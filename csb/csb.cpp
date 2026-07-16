#include "csb.hpp"

#include <format>
#include <string>

void csb::configure()
{
  csb::target_configuration = RELEASE;
  csb::include_files = {"csp.hpp"};
}

int csb::clean()
{
  csb::clean_build();
  return csb::build();
}

int csb::build()
{
  csb::generate_compile_commands(true);
  csb::generate_clangd({{"Diagnostics", {{"UnusedIncludes", "Strict"}, {"MissingIncludes", "Strict"}}}});
  csb::generate_clang_tidy({{"Checks", "-*, "
                                       "clang-analyzer-*, "
                                       "cppcoreguidelines-*, "
                                       "-cppcoreguidelines-avoid-non-const-global-variables, "
                                       "-cppcoreguidelines-avoid-const-or-ref-data-members, "
                                       "-cppcoreguidelines-non-private-member-variables-in-classes, "
                                       "-cppcoreguidelines-use-enum-class, "
                                       "-cppcoreguidelines-pro-type-union-access, "
                                       "-cppcoreguidelines-pro-type-reinterpret-cast, "
                                       "-cppcoreguidelines-pro-bounds-pointer-arithmetic, "
                                       "-cppcoreguidelines-owning-memory, "
                                       "-cppcoreguidelines-avoid-magic-numbers, "
                                       "llvm-*, "
                                       "-llvm-header-guard, "
                                       "-llvm-prefer-static-over-anonymous-namespace, "
                                       "-llvm-namespace-comment, "
                                       "-llvm-else-after-return, "
                                       "hicpp-*, "
                                       "-hicpp-named-parameter, "
                                       "-hicpp-braces-around-statements, "
                                       "-hicpp-uppercase-literal-suffix, "
                                       "-hicpp-avoid-c-arrays, "
                                       "-hicpp-no-malloc, "
                                       "cert-*, "
                                       "-cert-err58-cpp, "
                                       "bugprone-*, "
                                       "-bugprone-forward-declaration-namespace, "
                                       "-bugprone-easily-swappable-parameters, "
                                       "-bugprone-branch-clone, "
                                       "performance-*, "
                                       "concurrency-*, "
                                       "-concurrency-mt-unsafe, "
                                       "portability-*, "
                                       "-portability-avoid-pragma-once, "
                                       "readability-*, "
                                       "-readability-use-concise-preprocessor-directives, "
                                       "-readability-avoid-const-params-in-decls, "
                                       "-readability-isolate-declaration, "
                                       "-readability-redundant-access-specifiers, "
                                       "-readability-redundant-member-init, "
                                       "-readability-convert-member-functions-to-static, "
                                       "-readability-named-parameter, "
                                       "-readability-suspicious-call-argument, "
                                       "-readability-braces-around-statements, "
                                       "-readability-avoid-nested-conditional-operator, "
                                       "-readability-inconsistent-ifelse-braces, "
                                       "-readability-implicit-bool-conversion, "
                                       "-readability-else-after-return, "
                                       "-readability-magic-numbers, "
                                       "-readability-function-cognitive-complexity, "
                                       "-readability-uppercase-literal-suffix, "
                                       "modernize-*, "
                                       "-modernize-use-nodiscard, "
                                       "-modernize-use-designated-initializers, "
                                       "-modernize-use-trailing-return-type, "
                                       "-modernize-pass-by-value, "
                                       "misc-*, "
                                       "-misc-header-include-cycle, "
                                       "-misc-non-private-member-variables-in-classes, "
                                       "-misc-no-recursion"}});

  csb::generate_clang_format({{"BasedOnStyle", "LLVM"},
                              {"ColumnLimit", "120"},
                              {"IndentWidth", "2"},
                              {"ConstructorInitializerIndentWidth", "2"},
                              {"ContinuationIndentWidth", "2"},
                              {"Language", "Cpp"},
                              {"BreakBeforeBraces", "Allman"},
                              {"AllowShortBlocksOnASingleLine", "true"},
                              {"AllowShortIfStatementsOnASingleLine", "true"},
                              {"AllowShortCaseLabelsOnASingleLine", "true"},
                              {"AllowShortLoopsOnASingleLine", "true"},
                              {"AllowShortFunctionsOnASingleLine", "true"},
                              {"AllowShortLambdasOnASingleLine", "true"},
                              {"AllowShortEnumsOnASingleLine", "true"},
                              {"AllowShortNamespacesOnASingleLine", "true"},
                              {"BreakTemplateDeclarations", "No"},
                              {"IndentPPDirectives", "BeforeHash"},
                              {"IndentCaseLabels", "true"},
                              {"NamespaceIndentation", "All"},
                              {"FixNamespaceComments", "false"}});
  if (!csb::is_subproject) csb::format("22.1.8");

  auto build_include_path = csb::path("build/include/csp");
  if (!csb::exists(build_include_path)) csb::mkdir(build_include_path);
  csb::multi_task_run(std::format("{} () []", csb::host_platform == WINDOWS ? "copy /Y" : "cp"), csb::include_files,
                      {build_include_path / "(filename)"});
  return csb::run();
}

int csb::run() { return csb::success; }

CSB_MAIN()
