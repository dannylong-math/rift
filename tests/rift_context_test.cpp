#include <boost/ut.hpp>
#include <rift/rift_context.hpp>
#include <type_traits>

int main(int argc, char** argv)
{
    using namespace boost::ut;

    rift::RiftContext actual_context(argc, argv);

    // workaround to allow the context to be used inside the test suite lambda.
    static rift::RiftContext* context_ptr = nullptr;
    context_ptr = &actual_context;

    suite<"RiftContext"> suite = [] {
        auto& context = *context_ptr;
        "RiftContext borrows the world communicator"_test = [&context] {
            expect(context.mpi_communicator() == MPI_COMM_WORLD);
        };

        "RiftContext reports the world rank and size"_test = [&context] {
            int raw_rank = -1;
            int raw_size = -1;

            expect(MPI_SUCCESS == MPI_Comm_rank(MPI_COMM_WORLD, &raw_rank));
            expect(MPI_SUCCESS == MPI_Comm_size(MPI_COMM_WORLD, &raw_size));
            expect(0_i <= raw_rank);
            expect(raw_rank < raw_size);
            expect(1_i <= raw_size);
            expect(context.this_mpi_process() == static_cast<unsigned int>(raw_rank));
            expect(context.n_mpi_processes() == static_cast<unsigned int>(raw_size));
        };

        "RiftContext owns one non-transferable runtime lifetime"_test = [] {
            expect(not std::is_copy_constructible_v<rift::RiftContext>);
            expect(not std::is_copy_assignable_v<rift::RiftContext>);
            expect(not std::is_move_constructible_v<rift::RiftContext>);
            expect(not std::is_move_assignable_v<rift::RiftContext>);
        };

        "RiftContext accepts its default and unsigned thread limits"_test = [] {
            expect(std::is_constructible_v<rift::RiftContext, int&, char**&>);
            expect(std::is_constructible_v<rift::RiftContext, int&, char**&, unsigned int>);
        };
    };

    const auto result = static_cast<int>(cfg<>.run());
    context_ptr = nullptr; // avoid dangling pointer after lifetime ends
    return result;
}
