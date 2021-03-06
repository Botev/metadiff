//
// Created by alex on 18/12/15.
//

#ifndef AUTODIFF_BACKENDS_BASE_H
#define AUTODIFF_BACKENDS_BASE_H

struct stat info;
#include "sys/stat.h"

namespace metadiff{
    namespace backend {
        using namespace exceptions;

        /** Abstract class for a backend, which will generate and link code */
        template<typename T>
        class FunctionBackend {
        protected:
            std::shared_ptr<spdlog::logger> logger() const {
                return logging::logger("backend::" + name);
            }

            /** Name of the backend */
            std::string name;

            /** Handle to the underlying DLL */
            void *dll_handle;
        public:

            /** Path to directory used for logging and storing outputs */
            std::string dir_path;

            /** When this flag is on the generated code will not be optimal
             * and may contain different code for debugging */
            bool debug;

            /** The type of the function in the compiled library */
            typedef std::vector<T> (*func_ptr)(std::vector<T> &inputs, std::vector<SharedPtr> &shared);

            /** The list of constant variables
             * TODO Currently this is not used */
            std::vector<T> constant_variables;

            /** The actual function pointer */
            func_ptr eval_func;

            /** When called you don't need to pass the shared variables */
            std::vector<T> eval(std::vector<T> &inputs) {
                return eval_func(inputs, shared::shared_vars);
            }

            FunctionBackend(std::string name, bool debug = false) :
                    name(name),
                    debug(debug) {
                dir_path = os::make_temp_dir();
            };

            FunctionBackend(std::string name, std::string dir_path, bool debug = false) :
                    name(name),
                    dir_path(dir_path),
                    debug(debug) { };

            /** Any form of initialization required should be carried out here */
            virtual void initialize() { };

            /** Generates the source code to the path specified */
            virtual void generate_source(std::string source_dir,
                                         Graph graph,
                                         std::vector<Node> inputs,
                                         std::vector<Node> targets) = 0;

            /** Compiles the source file to a dynamic library */
            virtual void compile(std::string source_dir,
                                 std::string target_dir,
                                 std::string graph_name) = 0;

            /** Links all of the compiled files and returns the final
             * EvaluationFunction instance */
            virtual func_ptr link(std::string target_dir,
                                  std::string graph_name) = 0;

            /** Function to open and link the DLL specified */
            func_ptr link_dll(std::string dll_path, std::string symbol_name) {
                logger()->debug() << "Linking file " << dll_path;
                char *error_msg;
                dll_handle = dlopen((dll_path).c_str(), RTLD_LAZY);
                if (!dll_handle) {
                    CompilationFailed e = CompilationFailed("Error when opening DLL:" + std::string(dlerror()));
                    logger()->error() << e.msg;
                    throw e;
                }
                auto func_handle = (func_ptr) dlsym(dll_handle, symbol_name.c_str());
                if ((error_msg = dlerror()) != NULL) {
                    CompilationFailed e = CompilationFailed("Error when finding symbol:" + std::string(error_msg));
                    logger()->error() << e.msg;
                    throw e;
                }
                return func_handle;
            };

            /** Closes the opened underlying DLL. Any function calls after this will fail. */
            void close() {
                dlclose(dll_handle);
            }

            /** Compiles a function from the graph given the inputs, targets and extra updates */
            void compile_function(Graph graph,
                                  std::vector<Node> inputs,
                                  std::vector<Node> targets,
                                  Updates &updates) {
                logger()->debug() << "Compiling function to " << dir_path;
                os::create_dir(dir_path, true);
                // Set path for the source
                std::string source_dir = os::join_paths(dir_path, "src");
                os::create_dir(source_dir, true);

                // Generate the source
                graph->add_temporary_updates(updates);
                generate_source(source_dir, graph, inputs, targets);
                graph->clear_temporary_updates();

                // Set path for the lib
                std::string target_dir = os::join_paths(dir_path, "lib");
                os::create_dir(target_dir, true);

                // Compile the source to the lib
                compile(source_dir, target_dir, graph->name);

                // Open the DLL
                eval_func = link(target_dir, graph->name);
            }

            void write_interface(std::ofstream &f) {
                f << "namespace metadiff{\n"
                        "    namespace core {\n"
                        "        enum dType {\n"
                        "            /** 8 bit boolean */\n"
                        "                    b8 = 0,\n"
                        "            /** 8 bit unsigned integer */\n"
                        "                    u8 = 1,\n"
                        "            /** 16 bit unsigned integer */\n"
                        "                    u16 = 2,\n"
                        "            /** 32 bit unsigned integer */\n"
                        "                    u32 = 3,\n"
                        "            /** 64 bit unsigned integer */\n"
                        "                    u64 = 4,\n"
                        "            /** 8 bit signed integer */\n"
                        "                    i8 = 5,\n"
                        "            /** 16 bit signed integer */\n"
                        "                    i16 = 6,\n"
                        "            /** 32 bit signed integer */\n"
                        "                    i32 = 7,\n"
                        "            /** 64 bit signed integer */\n"
                        "                    i64 = 8,\n"
                        "            /** 8 bit floating point */\n"
                        "                    f8 = 9,\n"
                        "            /** 16 bit floating point */\n"
                        "                    f16 = 10,\n"
                        "            /** 32 bit floating point */\n"
                        "                    f32 = 11,\n"
                        "            /** 64 bit floating point */\n"
                        "                    f64 = 12\n"
                        "        };\n"
                        "    }\n"
                        "\n"
                        "    namespace shared{\n"
                        "        /** A shared variable is a like a static variable, which is synchronized between devices */\n"
                        "        class SharedVariable {\n"
                        "        public:\n"
                        "            size_t const id;\n"
                        "            std::string const name;\n"
                        "            std::array<long long, 4> const shape;\n"
                        "\n"
                        "//            af::array value;\n"
                        "        public:\n"
                        "            SharedVariable(size_t id,\n"
                        "                           std::array<long long, 4> shape,\n"
                        "                           std::string name):\n"
                        "                    id(id),\n"
                        "                    shape(shape),\n"
                        "                    name(name) {};\n"
                        "\n"
                        "            virtual core::dType get_dtype() const = 0;\n"
                        "        };\n"
                        "\n"
                        "        typedef std::shared_ptr<SharedVariable> SharedPtr;\n"
                        "        static std::vector<SharedPtr> shared_vars;\n"
                        "\n"
                        "    }\n"
                        "}\n"
                        "\n"
                        "using metadiff::shared::SharedVariable;\n"
                        "using metadiff::shared::SharedPtr;\n";
            }
        };
    }
}
#endif //AUTODIFF_BACKENDS_BASE_H
