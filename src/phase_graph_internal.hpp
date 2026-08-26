#pragma once

/**
 * \file
 * \brief Private collective operations used by deterministic phase-graph construction.
 */

#include "run_configuration_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <mpi.h>
#include <rift/phase_graph.hpp>
#include <rift/run_configuration.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rift::detail {

/** \brief MPI adapter used to gather one encoded byte count from every rank. */
using GatherByteCounts = int (*)(std::uint64_t, std::span<std::uint64_t>, MPI_Comm);

/**
 * \brief Bundle buffers for one exact variable-length byte gather.
 *
 * \code{.cpp}
 * std::array counts{3, 3};
 * std::array displacements{0, 3};
 * std::array<char, 6> gathered{};
 * const ExactByteGatherRequest request{
 *     .local_bytes = "gas",
 *     .counts = counts,
 *     .displacements = displacements,
 *     .gathered_bytes = gathered,
 * };
 * \endcode
 */
struct ExactByteGatherRequest {
    /** \brief Bytes contributed by this rank. */
    std::string_view local_bytes;
    /** \brief Byte count contributed by each rank. */
    std::span<const int> counts;
    /** \brief Starting output offset for each rank. */
    std::span<const int> displacements;
    /** \brief Contiguous destination for every rank's bytes. */
    std::span<char> gathered_bytes;
};

/** \brief MPI adapter used to gather exact variable-length byte payloads. */
using GatherExactBytes = int (*)(const ExactByteGatherRequest&, MPI_Comm);

/** \brief Private allocator for gathered encoded byte counts. */
using UInt64BufferAllocator = std::vector<std::uint64_t> (*)(std::size_t);

/** \brief Private allocator for MPI integer count and displacement buffers. */
using IntBufferAllocator = std::vector<int> (*)(std::size_t);

/** \brief Private allocator for gathered exact bytes. */
using ByteBufferAllocator = std::string (*)(std::size_t);

/**
 * \brief Own canonical declarations and their exact collective encoding.
 *
 * The declaration vectors are sorted independently of input order. `encoded`
 * is an internal length-prefixed comparison payload, not a persistence format.
 *
 * \code{.cpp}
 * const std::vector<rift::PhaseSpecification> phases{
 *     {"liquid", "low-mach"}, {"gas", "compressible"}};
 * const std::vector<rift::InterfaceSpecification> interfaces{
 *     {"surface", "liquid", "gas", "finite-rate"}};
 * const auto canonical = rift::detail::allocate_canonical_phase_graph_input(
 *     phases, interfaces, true);
 * // canonical.phases begins with "gas" and canonical.encoded is ready for
 * // exact collective comparison.
 * \endcode
 */
struct CanonicalPhaseGraphInput {
    /** \brief Phase declarations sorted by all exact logical fields. */
    std::vector<PhaseSpecification> phases;
    /** \brief Interface declarations sorted by all exact logical fields. */
    std::vector<InterfaceSpecification> interfaces;
    /** \brief Length-prefixed bytes including callback availability. */
    std::string encoded;
};

/** \brief Private allocation seam for canonical graph input. */
using CanonicalPhaseGraphInputAllocator = CanonicalPhaseGraphInput (*)(const std::vector<PhaseSpecification>&,
                                                                       const std::vector<InterfaceSpecification>&,
                                                                       bool);

/**
 * \brief Record one collectively agreed compatibility rejection.
 *
 * The operation is explicit because its diagnostic allocations occur between
 * canonical callback steps. Production uses
 * `record_compatibility_rejection`; private tests inject allocation failure to
 * verify that no rank advances to the next callback collective.
 */
using CompatibilityRejectionRecorder = void (*)(PhaseGraphErrors&, const InterfaceSpecification&,
                                                const PhaseDescriptor&, const PhaseDescriptor&, std::string_view);

/**
 * \brief Collect MPI operations needed for exact graph-input agreement.
 *
 * Production binds these entries to MPI. Private tests replace one entry at a
 * time to prove that an MPI status failure enters the run's fatal handler.
 *
 * \code{.cpp}
 * rift::detail::PhaseGraphCollectiveOperations operations{
 *     .communicator_size = MPI_Comm_size,
 *     .gather_byte_counts =
 *         +[](std::uint64_t local, std::span<std::uint64_t> lengths,
 *             MPI_Comm communicator) {
 *           return MPI_Allgather(&local, 1, MPI_UINT64_T, lengths.data(), 1,
 *                                MPI_UINT64_T, communicator);
 *         },
 *     .gather_exact_bytes =
 *         +[](const rift::detail::ExactByteGatherRequest &request,
 *             MPI_Comm communicator) {
 *           return MPI_Allgatherv(
 *               request.local_bytes.data(),
 *               static_cast<int>(request.local_bytes.size()), MPI_BYTE,
 *               request.gathered_bytes.data(), request.counts.data(),
 *               request.displacements.data(), MPI_BYTE, communicator);
 *         },
 *     .allocate_uint64_buffer = allocate_uint64_buffer,
 *     .allocate_int_buffer = allocate_int_buffer,
 *     .allocate_byte_buffer = allocate_byte_buffer,
 *     .collective_maximum = maximum,
 *     .abort = MPI_Abort,
 * };
 * \endcode
 */
struct PhaseGraphCollectiveOperations {
    /** \brief Query the number of ranks participating in graph construction. */
    MpiCommSize communicator_size;
    /** \brief Gather every rank's encoded byte count. */
    GatherByteCounts gather_byte_counts;
    /** \brief Gather every rank's exact encoded bytes. */
    GatherExactBytes gather_exact_bytes;
    /** \brief Allocate gathered per-rank byte lengths. */
    UInt64BufferAllocator allocate_uint64_buffer;
    /** \brief Allocate MPI counts and displacements. */
    IntBufferAllocator allocate_int_buffer;
    /** \brief Allocate the gathered byte payload. */
    ByteBufferAllocator allocate_byte_buffer;
    /** \brief Agree whether any rank observed a callback exception. */
    CollectiveMaximum collective_maximum;
    /** \brief Fatal handler retained by the run control. */
    MpiAbort abort;
};

/**
 * \brief Compare arbitrary bytes exactly across a run communicator.
 *
 * Lengths and contents are gathered without hashing, so equality does not
 * depend on a collision assumption. A logical difference returns `false` on
 * every rank. MPI status, representability, and local allocation failures are
 * fatal because a rank cannot safely leave this collective sequence alone.
 *
 * \param communicator run-owned intracommunicator.
 * \param local_bytes length-delimited local encoding to compare.
 * \param operations injected collective and fatal operations.
 * \return true exactly when every rank supplied the same bytes.
 */
[[nodiscard]] bool collectively_equal_bytes(MPI_Comm communicator, std::string_view local_bytes,
                                            const PhaseGraphCollectiveOperations& operations);

/**
 * \brief Agree whether any rank observed a local condition.
 * \param communicator run-owned intracommunicator.
 * \param local_condition condition observed on this rank.
 * \param operations injected maximum collective and fatal operation.
 * \return communicator-wide logical disjunction.
 */
[[nodiscard]] bool collective_any(MPI_Comm communicator, bool local_condition,
                                  const PhaseGraphCollectiveOperations& operations);

/** \brief Allocate a value-initialized `uint64_t` collective buffer. */
[[nodiscard]] std::vector<std::uint64_t> allocate_uint64_buffer(std::size_t size);

/** \brief Allocate a value-initialized MPI integer collective buffer. */
[[nodiscard]] std::vector<int> allocate_int_buffer(std::size_t size);

/** \brief Allocate a value-initialized exact-byte collective buffer. */
[[nodiscard]] std::string allocate_byte_buffer(std::size_t size);

/**
 * \brief Canonicalize and encode graph input using production storage.
 * \param phases phase declarations in arbitrary order.
 * \param interfaces interface declarations in arbitrary order.
 * \param callback_available whether a compatibility callback was supplied.
 * \return canonical declarations and exact comparison bytes.
 * \throws std::bad_alloc when local canonical storage cannot be allocated.
 */
[[nodiscard]] CanonicalPhaseGraphInput
allocate_canonical_phase_graph_input(const std::vector<PhaseSpecification>& phases,
                                     const std::vector<InterfaceSpecification>& interfaces, bool callback_available);

/**
 * \brief Append the diagnostic for one collectively agreed callback rejection.
 * \param errors destination graph errors.
 * \param interface_specification rejected oriented interface declaration.
 * \param minus resolved minus-phase descriptor.
 * \param plus resolved plus-phase descriptor.
 * \param reason exact collectively agreed rejection reason.
 * \throws std::bad_alloc when diagnostic storage cannot be allocated.
 */
void record_compatibility_rejection(PhaseGraphErrors& errors, const InterfaceSpecification& interface_specification,
                                    const PhaseDescriptor& minus, const PhaseDescriptor& plus, std::string_view reason);

/**
 * \brief Implement collective graph construction with an allocation seam.
 *
 * Public construction supplies `allocate_canonical_phase_graph_input`.
 * Private tests inject allocation failure and retain the run control's fatal
 * handler, proving a rank cannot enter the next collective alone.
 *
 * \param run run whose communicator and fatal policy govern construction.
 * \param phase_specifications phase declarations in arbitrary order.
 * \param interface_specifications interface declarations in arbitrary order.
 * \param compatibility_check deterministic registry query.
 * \param input_allocator canonical-input allocation operation.
 * \param rejection_recorder diagnostic operation between callback steps.
 * \return graph or communicator-consistent structured errors.
 */
[[nodiscard]] PhaseGraphResult make_phase_graph_with_input_allocator(
    const RunConfiguration& run, const std::vector<PhaseSpecification>& phase_specifications,
    const std::vector<InterfaceSpecification>& interface_specifications,
    const InterfaceCompatibilityCheck& compatibility_check, CanonicalPhaseGraphInputAllocator input_allocator,
    CompatibilityRejectionRecorder rejection_recorder = record_compatibility_rejection);

} // namespace rift::detail
