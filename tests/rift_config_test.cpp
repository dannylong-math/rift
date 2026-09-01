#include <boost/ut.hpp>
#include <rift/rift_config.hpp>
#include <type_traits>

int main(int argc, char** argv)
{
    using namespace boost::ut;

    rift::RiftConfig actual_config(argc, argv);

    // workaround to allow the config to be used inside the test suite lambda.
    static rift::RiftConfig* config_ptr = nullptr;
    config_ptr = &actual_config;

    suite<"RiftConfig"> suite = [] {
        auto& config = *config_ptr;
        "RiftConfig borrows the world communicator"_test = [&config] {
            expect(config.mpi_communicator() == MPI_COMM_WORLD);
        };

        "RiftConfig reports the world rank and size"_test = [&config] {
            int raw_rank = -1;
            int raw_size = -1;

            expect(MPI_SUCCESS == MPI_Comm_rank(MPI_COMM_WORLD, &raw_rank));
            expect(MPI_SUCCESS == MPI_Comm_size(MPI_COMM_WORLD, &raw_size));
            expect(0_i <= raw_rank);
            expect(raw_rank < raw_size);
            expect(1_i <= raw_size);
            expect(config.this_mpi_process() == static_cast<unsigned int>(raw_rank));
            expect(config.n_mpi_processes() == static_cast<unsigned int>(raw_size));
        };

        "RiftConfig owns one non-transferable runtime lifetime"_test = [] {
            expect(not std::is_copy_constructible_v<rift::RiftConfig>);
            expect(not std::is_copy_assignable_v<rift::RiftConfig>);
            expect(not std::is_move_constructible_v<rift::RiftConfig>);
            expect(not std::is_move_assignable_v<rift::RiftConfig>);
        };

        "RiftConfig accepts its default and unsigned thread limits"_test = [] {
            expect(std::is_constructible_v<rift::RiftConfig, int&, char**&>);
            expect(std::is_constructible_v<rift::RiftConfig, int&, char**&, unsigned int>);
        };
    };

    const auto result = static_cast<int>(cfg<>.run());
    config_ptr = nullptr; // avoid dangling pointer after lifetime ends
    return result;
}
