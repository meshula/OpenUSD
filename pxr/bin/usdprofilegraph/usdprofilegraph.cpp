//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/pxr.h"
#include "pxr/base/tf/pxrCLI11/CLI11.h"
#include "pxr/usd/usdProfiles/profileRegistry.h"
#include "pxr/usd/usdProfiles/profilesDocUtils.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

/*
The usdprofiles tool can generate Markdown, Mermaid, and GraphViz DOT
documentation from the UsdProfiles ProfileRegistry.

Running with no arguments will run against the live Profiles registry and 
generate sample outputs as populated by the installed plugins.

New profile graphs can be tested by passing a path to a json file to test, and
it will be loaded via the _TestLoadFromFile hook.
*/

// Using the test fixtures to enable the loading of prototype plugInfo.json
// files.
PXR_NAMESPACE_OPEN_SCOPE
USDPROFILES_API void Usd_ProfilesRegistryTestClear();
USDPROFILES_API bool Usd_ProfilesRegistryTestLoadFromFile(
    const std::string& filePath,
    std::vector<std::string>* errors = nullptr);
PXR_NAMESPACE_CLOSE_SCOPE

PXR_NAMESPACE_USING_DIRECTIVE
using namespace pxr_CLI;
namespace {
    struct Args {
        std::string fixture;
        std::string output;
        bool verbose = false;
        bool markdown = false;
        bool markdown_mermaid = false;
        bool mermaid = false;
        bool dot = false;
        bool validate = false;
        std::string perspective;
        bool claim = false;
        std::string layer;
        std::string prim;
    };
} // anon

int main(int argc, char const *argv[]) {
    CLI::App app(
R"(The usdprofiles tool can generate Markdown, Mermaid, and GraphViz DOT
documentation from the UsdProfiles ProfileRegistry.
Running with no arguments will run against the live Profiles registry and 
generate sample outputs as populated by the installed plugins.
New profile graphs can be tested by passing a path to a json file to test.
Use the mermaid and markdown flags together to embed mermaid in markdown.
)");

    Args args;
    app.add_option(
        "-j, --pluginfo", args.fixture, "A pluginfo.json to process");
    app.add_option(
        "-v, --verbose", args.verbose, "Verbose debug output");
    app.add_option(
        "-o, --output", args.output, "output directory");
    app.add_option(
        "-m, --markdown", args.markdown, "output markdown");
    app.add_option(
        "-r, --mermaid", args.markdown_mermaid, "output markdown with mermaid");
    app.add_option(
        "-d, --dot", args.dot, "output dot");
    app.add_option(
        "--validate", args.validate, "perform validation");
    app.add_option(
        "-p, --perspective", args.perspective, "claim root");
    app.add_option(
        "-c, --claim", args.claim, "create claims from implicit information");
    app.add_option(
        "-l, --layer", args.layer, "output claims to a named layer");
    app.add_option(
        "-t, --target", args.prim, "write claims to this prims");

    CLI11_PARSE(app, argc, argv);

    if (args.fixture.size() > 0) {
        Usd_ProfilesRegistryTestClear();
        std::vector<std::string> errors;
        bool ok = Usd_ProfilesRegistryTestLoadFromFile(args.fixture, &errors);
        if (ok) {
            std::cout << "Using UsdProfiles from: " << args.fixture << std::endl;            
        }
        else {
            std::cout << "Could not load UsdProfiles from: " << args.fixture << "\n";
            for (auto& i : errors) {
                std::cout << "    " << i << "\n";
            }
            std::cout << std::endl;
            return 1;
        }
    }
    else {
        std::cout << "Using installed UsdProfiles" << std::endl;
    }

    if (args.verbose) {
        auto caps = UsdProfileRegistry::GetAllCapabilities();
        auto profiles = UsdProfileRegistry::GetAllProfiles();
    
        std::cout << "\nCapabilities (" << caps.size() << ")-------------\n";
        for (auto& i : caps) {
            std::cout << "  " << i.GetString() << "\n";
        }
        std::cout << "\n\nProfiles (" << profiles.size() << ")-------------\n";
        for (auto& i : profiles) {
            std::cout << "  " << i.GetString() << "\n";
        }
        std::cout << std::endl;
    }

    if (args.output.size() > 0) {
        std::filesystem::create_directories(args.output);
    }

    std::filesystem::path base_dir = args.output.empty() 
            ? std::filesystem::current_path() 
            : std::filesystem::path(args.output);


    auto write_file = [&](std::string_view filename, std::string_view content) -> bool {
        std::ofstream file(base_dir / filename);
        if (!file.is_open()) {
            return false;
        }
        file << content;
        return true;
    };

    std::vector<std::string> outputs;
    
    if (args.dot) {
        std::string fname = "profiles.dot";
        write_file(fname, UsdProfilesDot());
        outputs.push_back(fname);
    }

    if (args.markdown && args.mermaid) {
        std::map<std::string, std::string> styles; // empty
        std::string fname = "profiles_graph.md.dot";
        write_file(fname, UsdProfilesMarkdown(styles));
        outputs.push_back(fname);
    }
    else {
        if (args.markdown) {
            std::string fname = "profiles.md";
            write_file(fname, UsdProfilesMarkdown());
            outputs.push_back(fname);
        }
        if (args.mermaid) {
            std::map<std::string, std::string> styles; // empty
            std::string fname = "profiles.md";
            write_file(fname, UsdProfilesMermaid(styles));
            outputs.push_back(fname);
        }
    }

    if (!outputs.size()) {
        std::cerr << "No output formats specified; no output generated" << std::endl;
        return 1;
    }

    std::cout << "Outputs generated:\n";
    for (auto& i : outputs) {
        std::cout << "   " << i << "\n";
    }
    std::cout << std::endl;
    return 0;
}
