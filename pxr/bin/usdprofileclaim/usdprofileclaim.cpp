//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/pxr.h"
#include "pxr/base/tf/pxrCLI11/CLI11.h"
#include "pxr/base/tf/errorMark.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/js/json.h"
#include "pxr/base/js/types.h"
#include "pxr/base/js/value.h"
#include "pxr/usd/usdProfiles/claimsAPI.h"
#include "pxr/usd/usdProfiles/profileRegistry.h"
#include "pxr/usd/usdProfiles/profilesDocUtils.h"
#include "pxr/usd/usd/editContext.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/primRange.h"
#include "pxr/usd/usd/primTypeInfo.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/path.h"
#include <filesystem>
#include <iostream>
#include <ostream>
#include <string>
#include <vector>

/*
    usdprofileclaim -- explicit capability claims, verification, and prim
    introspection for OpenUSD.

    Authors and verifies profile capability claims on OpenUSD stages and prims.
*/

// Using the test fixtures to enable the loading of prototype plugInfo.json
// files that override the capability graph baked into the OpenUSD libraries.
PXR_NAMESPACE_OPEN_SCOPE

USDPROFILES_API 
void Usd_ProfilesRegistryTestClear();

USDPROFILES_API 
bool Usd_ProfilesRegistryTestLoadFromFile(
    const std::string& filePath,
    std::vector<std::string>* errors = nullptr);

PXR_NAMESPACE_CLOSE_SCOPE

PXR_NAMESPACE_USING_DIRECTIVE
using namespace pxr_CLI;

namespace {

// Using several exit codes in case it is useful for automation.
enum class ExitStatus : int {
    Success = 0,             // Verification passed / expected state (ValidPath).
    VerificationFailed = 1,  // NoPath, missing capability, or invalid profile.
    GraphConflict = 2,       // DeprecationConflict or CycleFound.
    InvalidArguments = 3,    // Invalid CLI arguments or path not found.
};

struct Args {
    // Mode selection (exactly one is required).
    bool introspect = false;                    // -i, --introspect
    bool verify = false;                        // -v, --verify
    bool report = false;                        // -R, --report
    std::vector<std::string> claimCapability;   // -c, --claim-capability CAPABILITY=CLASS
    std::string claimProfile;                   // -p, --claim-profile PROFILE

    // Introspection & claim operations.
    std::string prim = "/";                     // -s, --prim PATH
    bool write = false;                         // -w, --write
    std::vector<std::string> except;            // -e, --except CAPABILITY
    std::string pluginfo;                       // --pluginfo pluginfo.json
    std::string layer;                          // -l, --layer LAYER

    // Filtering & output.
    bool json = false;                          // -j, --json
    bool verbose = false;                       // --verbose (long-only; -v is --verify)

    // Positional.
    std::string usdFile;                        // file.usd
};

//-----------------------------------------------------------------------------
// Profile and Capability Utility functions.
//

// Normalize a degradation class token to its canonical form. Accepts "hard",
// "soft", "enh", and "enhancement". Returns "" if the token is not recognized.
std::string NormalizeDegradationClass(const std::string& token)
{
    if (token == "hard" || token == "h") {
        return "hard";
    }
    if (token == "soft" || token == "s") {
        return "soft";
    }
    if (token == "enhancement" || token == "e") {
        return "enhancement";
    }
    return "";
}

// Parse a single "CAPABILITY=CLASS" argument to --claim-capability. On success
// fills *capability and *degradationClass (canonicalized) and returns true. On
// failure sets *error and returns false.
bool ParseCapabilityClaim(const std::string& arg,
                          std::string* capability,
                          std::string* degradationClass,
                          std::string* error)
{
    const auto eq = arg.find('=');
    if (eq == std::string::npos) {
        *error = "expected CAPABILITY=CLASS, got '" + arg + "'";
        return false;
    }
    std::string cap = arg.substr(0, eq);
    std::string cls = arg.substr(eq + 1);
    if (cap.empty()) {
        *error = "missing capability name in '" + arg + "'";
        return false;
    }
    const std::string normalized = NormalizeDegradationClass(cls);
    if (normalized.empty()) {
        *error = "invalid degradation class '" + cls +
                 "' (expected one of: hard, soft, enhancement, h, s, or e)";
        return false;
    }
    *capability = std::move(cap);
    *degradationClass = normalized;
    return true;
}

// Load the capability/profile graph. When args.pluginfo is set, loads it via
// the test hook; otherwise uses the installed UsdProfiles plugins. Returns
// false on load failure.
bool LoadProfileGraph(const Args& args)
{
    if (args.pluginfo.empty()) {
        std::cout << "Using installed UsdProfiles" << std::endl;
        return true;
    }
    Usd_ProfilesRegistryTestClear();
    std::vector<std::string> errors;
    if (Usd_ProfilesRegistryTestLoadFromFile(args.pluginfo, &errors)) {
        std::cout << "Using UsdProfiles from: " << args.pluginfo << std::endl;
        return true;
    }
    std::cerr << "Could not load UsdProfiles from: " << args.pluginfo << "\n";
    for (const auto& e : errors) {
        std::cerr << "    " << e << "\n";
    }
    std::cerr << std::flush;
    return false;
}

// Extract a capability's degradation-class string from the VtValue stored in a
// capabilityUsages dict.
// Fall back to a string or stringified value.
std::string DegradationClassString(const VtValue& value)
{
    if (value.IsHolding<TfToken>()) {
        return value.UncheckedGet<TfToken>().GetString();
    }
    if (value.IsHolding<std::string>()) {
        return value.UncheckedGet<std::string>();
    }
    return TfStringify(value);
}

// Build a JSON object {capability: class, ...} from a capability-usage dict
JsObject CapabilityDictToJs(const VtDictionary& dict)
{
    JsObject obj;
    for (const auto& kv : dict) {
        obj[kv.first] = JsValue(DegradationClassString(kv.second));
    }
    return obj;
}

//-----------------------------------------------------------------------------
// Reporting functions.
//

// Build the profilesInfo JSON object as an array of
// {profile, exceptions:[...]} objects.
JsObject ProfilesInfoToJs(const UsdProfilesClaimsAPI& api)
{
    JsObject obj;
    obj["capabilityUsages"] = JsValue(CapabilityDictToJs(
        api.GetCapabilityUsages()));

    JsArray profiles;
    for (const TfToken& profile : api.GetCompatibleProfiles()) {
        JsObject entry;
        entry["profile"] = JsValue(profile.GetString());
        JsArray exceptions;
        for (const TfToken& e : api.GetProfileExceptions(profile)) {
            exceptions.push_back(JsValue(e.GetString()));
        }
        entry["exceptions"] = JsValue(std::move(exceptions));
        profiles.push_back(JsValue(std::move(entry)));
    }
    obj["profileCompatibility"] = JsValue(std::move(profiles));
    return obj;
}

// Return the profilesInfo JSON for a prim's current state.
JsValue PrimStateToJs(const UsdPrim& prim)
{
    if (prim.HasAPI<UsdProfilesClaimsAPI>()) {
        return JsValue(ProfilesInfoToJs(UsdProfilesClaimsAPI(prim)));
    }

    // return an empty object if CLaimsAPI is not applied.
    JsObject obj;
    obj["capabilityUsages"] = JsValue(JsObject());
    obj["profileCompatibility"] = JsValue(JsArray());
    return JsValue(std::move(obj));
}

// Pretty-print a single JSON report object.
void EmitJson(std::ostream& out, JsObject jsonReport)
{
    JsWriteToStream(JsValue(std::move(jsonReport)), std::cout);
    out << "\n";
}

// Dump the prim's authored capability usages and per-profile
// exception sets as plain text.
void PrintProfilesInfo(std::ostream& out,
                       const UsdProfilesClaimsAPI& api)
{
    const VtDictionary usages = api.GetCapabilityUsages();
    out << "  capabilityUsages:";
    if (usages.empty()) {
        out << " (none)\n";
    }
    else {
        out << "\n";
        for (const auto& kv : usages) {
            out << "    " << kv.first << " = "
                << DegradationClassString(kv.second) << "\n";
        }
    }

    const VtArray<TfToken> profiles = api.GetCompatibleProfiles();
    out << "  profileCompatibility:";
    if (profiles.empty()) {
        out << " (none)\n";
    }
    else {
        out << "\n";
        for (const TfToken& profile : profiles) {
            out << "    " << profile;
            const VtArray<TfToken> exceptions =
                api.GetProfileExceptions(profile);
            if (exceptions.empty()) {
                out << " (no exceptions)\n";
            }
            else {
                out << " except [";
                for (size_t i = 0; i < exceptions.size(); ++i) {
                    out << (i ? ", " : "") << exceptions[i];
                }
                out << "]\n";
            }
        }
    }
}


//-----------------------------------------------------------------------------
// Stage and prim functions.
//

// Open the stage specified by args.usdFile. Existence has already been
// validated by the caller. Returns a null stage on failure to load or
// other errors.
UsdStageRefPtr OpenStage(const Args& args, TfErrorMark& mark)
{
    UsdStageRefPtr stage = UsdStage::Open(args.usdFile);
    if (!stage) {
        std::cerr << "error: failed to open stage: " << args.usdFile << "\n";
        return nullptr;
    }
    if (!mark.IsClean()) {
        std::cerr << "error: errors encountered while opening stage: "
                  << args.usdFile << "\n";
        for (const TfError& e : mark) {
            std::cerr << "    " << e.GetCommentary() << "\n";
        }
        return nullptr;
    }
    return stage;
}

// Resolve the target prim from args.prim on the given stage. Validates the
// path string syntactically.
//
// Returns an invalid UsdPrim if the path string is malformed (after printing
// an error) or if no prim exists at the path.
UsdPrim GetTargetPrim(const UsdStageRefPtr& stage, const Args& args)
{
    std::string err;
    if (!SdfPath::IsValidPathString(args.prim, &err)) {
        std::cerr << "error: invalid prim path '" << args.prim
                  << "': " << err << "\n";
        return UsdPrim();
    }
    return stage->GetPrimAtPath(SdfPath(args.prim));
}

// Resolve the edit target for authoring.
// Defaults to writing to the stage's root layer;
// if args.layer is set, the named layer must be part of the stage's local
// layer stack. On failure, prints an error and sets *ok to false.
UsdEditTarget ResolveEditTarget(const UsdStageRefPtr& stage,
                                const Args& args,
                                bool* ok)
{
    *ok = true;
    if (args.layer.empty()) {
        return UsdEditTarget(stage->GetRootLayer());
    }

    SdfLayerRefPtr layer = SdfLayer::FindOrOpen(args.layer);
    if (!layer) {
        std::cerr << "error: could not find or open layer: " << args.layer
                  << "\n";
        *ok = false;
        return UsdEditTarget();
    }
    if (!stage->HasLocalLayer(layer)) {
        std::cerr << "error: layer is not in the stage's local layer stack: "
                  << args.layer << "\n";
        *ok = false;
        return UsdEditTarget();
    }
    return stage->GetEditTargetForLocalLayer(layer);
}


// Print the target prim's current profile claims. Read-only.
ExitStatus RunReport(std::ostream& out, const Args& args)
{
    TfErrorMark mark;
    UsdStageRefPtr stage = OpenStage(args, mark);
    if (!stage) {
        std::cerr << "Could not open stage" << "\n";
        return ExitStatus::InvalidArguments;
    }

    UsdPrim prim = GetTargetPrim(stage, args);
    if (!prim) {
        std::cerr << "error: no prim at path: " << args.prim << "\n";
        return ExitStatus::VerificationFailed;
    }

    if (args.json) {
        JsObject jsonReport;
        jsonReport["prim"] = JsValue(prim.GetPath().GetString());
        jsonReport["mode"] = JsValue(std::string("report"));
        jsonReport["profilesInfo"] = PrimStateToJs(prim);
        EmitJson(out, std::move(jsonReport));
        return ExitStatus::Success;
    }

    std::cout << "profilesInfo for <" << prim.GetPath() << ">:\n";
    if (!prim.HasAPI<UsdProfilesClaimsAPI>()) {
        std::cout << "  (ClaimsAPI not applied; no claims)\n";
        return ExitStatus::Success;
    }

    UsdProfilesClaimsAPI api(prim);
    PrintProfilesInfo(out, api);
    return ExitStatus::Success;
}


//-----------------------------------------------------------------------------
// Deeply inspect the target prim and its descendants, inferring capabilities
// from applied API schemas and layer file formats.
// If the tool was run with --write, this routine will populate
// capabilityUsages on the stage.
ExitStatus RunIntrospect(const Args& args)
{
    TfErrorMark mark;
    UsdStageRefPtr stage = OpenStage(args, mark);
    if (!stage) {
        return ExitStatus::InvalidArguments;
    }

    UsdPrim prim = GetTargetPrim(stage, args);
    if (!prim) {
        std::cerr << "error: no prim at path: " << args.prim << "\n";
        return ExitStatus::VerificationFailed;
    }

    JsObject jsonReport;
    if (args.json) {
        jsonReport["mode"] = JsValue(std::string("introspect"));
        jsonReport["write"] = JsValue(args.write);
    }

    // If the user asked to author on the pseudo-root, there is no place to
    // actually author results, nor can ComputeCapabilityUsages succeed where
    // no APIs can be applied. Fall back to the stage's defaultPrim when the
    // user set --prim as "/".
    if (prim.IsPseudoRoot()) {
        UsdPrim defaultPrim = stage->GetDefaultPrim();
        if (!defaultPrim) {
            std::cerr << "error: no prim specified and the stage has no "
                         "defaultPrim; pass --prim PATH\n";
            return ExitStatus::InvalidArguments;
        }
        if (args.json) {
            jsonReport["defaultPrimFallback"] = JsValue(true);
        }
        else {
            std::cout << "note: no --prim given; introspecting defaultPrim <"
                      << defaultPrim.GetPath() << ">\n";
        }
        prim = defaultPrim;
    }

    if (args.json) {
        jsonReport["prim"] = JsValue(prim.GetPath().GetString());
        jsonReport["before"] = PrimStateToJs(prim);
    }
    else {
        // Show the prim's claims before any change are made by the tool.
        std::cout << "profilesInfo (before) for <" << prim.GetPath() << ">:\n";
        if (prim.HasAPI<UsdProfilesClaimsAPI>()) {
            PrintProfilesInfo(std::cout, UsdProfilesClaimsAPI(prim));
        }
        else {
            std::cout << "  (ClaimsAPI not applied; no claims)\n";
        }
    }

    if (!args.write) {
        // Dry-run: compute inferred capabilities without applying the schema
        // or authoring anything. An unapplied API is fine for compute.
        UsdProfilesClaimsAPI api(prim);
        VtDictionary inferred = api.ComputeCapabilityUsages();
        if (!mark.IsClean()) {
            std::cerr << "error: errors while inferring capabilities:\n";
            for (const TfError& e : mark) {
                std::cerr << "    " << e.GetCommentary() << "\n";
            }
            return ExitStatus::VerificationFailed;
        }
        if (args.json) {
            jsonReport["inferred"] = JsValue(CapabilityDictToJs(inferred));
            EmitJson(std::cout, std::move(jsonReport));
            return ExitStatus::Success;
        }
        std::cout << "would infer (dry-run; pass --write to author):\n";
        if (inferred.empty()) {
            std::cout << "    (none)\n";
        }
        else {
            for (const auto& kv : inferred) {
                std::cout << "    " << kv.first << " = "
                          << DegradationClassString(kv.second) << "\n";
            }
        }
        return ExitStatus::Success;
    }


    // If write was not specified, we already returned.
    //
    // --write: apply the schema, populate usages under the resolved edit
    // target, and save.
    bool ok = false;
    UsdEditTarget editTarget = ResolveEditTarget(stage, args, &ok);
    if (!ok) {
        return ExitStatus::InvalidArguments;
    }

    {
        UsdEditContext editContext(stage, editTarget);
        UsdProfilesClaimsAPI api = UsdProfilesClaimsAPI::Apply(prim);
        if (!api) {
            std::cerr << "error: could not apply ClaimsAPI to <"
                      << prim.GetPath() << ">\n";
            return ExitStatus::VerificationFailed;
        }
        api.PopulateCapabilityUsages();
    }

    if (!mark.IsClean()) {
        std::cerr << "error: errors while populating capability usages:\n";
        for (const TfError& e : mark) {
            std::cerr << "    " << e.GetCommentary() << "\n";
        }
        return ExitStatus::VerificationFailed;
    }

    stage->Save();
    if (!mark.IsClean()) {
        std::cerr << "error: errors while saving stage:\n";
        for (const TfError& e : mark) {
            std::cerr << "    " << e.GetCommentary() << "\n";
        }
        return ExitStatus::VerificationFailed;
    }

    if (args.json) {
        jsonReport["after"] = PrimStateToJs(prim);
        EmitJson(std::cout, std::move(jsonReport));
        return ExitStatus::Success;
    }
    std::cout << "profilesInfo (after) for <" << prim.GetPath() << ">:\n";
    PrintProfilesInfo(std::cout, UsdProfilesClaimsAPI(prim));
    return ExitStatus::Success;
}

// Verify declared capability usages and profile compatibility claims against
// the UsdProfileRegistry DAG.
ExitStatus RunVerify(const Args& args)
{
    /// @TODO XXX
    (void)args;
    std::cerr << "[stub] --verify not yet implemented\n";
    return ExitStatus::Success;
}

// Author or update capability usage entries on the target prim.
// Function will dry-run by default (printing intended changes).
// With the --write argument, the ClaimsAPI will be used to author each usage,
// and then the stage will be saved.
ExitStatus RunClaimCapability(const Args& args)
{
    TfErrorMark mark;
    UsdStageRefPtr stage = OpenStage(args, mark);
    if (!stage) {
        return ExitStatus::InvalidArguments;
    }

    UsdPrim prim = GetTargetPrim(stage, args);
    if (!prim) {
        std::cerr << "error: no prim at path: " << args.prim << "\n";
        return ExitStatus::VerificationFailed;
    }

    // Under --json, everything is folded into a single jsonReport object emitted
    // once at the end.
    JsObject jsonReport;
    if (args.json) {
        jsonReport["mode"] = JsValue(std::string("claim-capability"));
        jsonReport["write"] = JsValue(args.write);
        jsonReport["prim"] = JsValue(prim.GetPath().GetString());
        jsonReport["before"] = PrimStateToJs(prim);
    }
    else {
        // Show the prim's claims before any change.
        std::cout << "profilesInfo (before) for <" << prim.GetPath() << ">:\n";
        if (prim.HasAPI<UsdProfilesClaimsAPI>()) {
            PrintProfilesInfo(std::cout, UsdProfilesClaimsAPI(prim));
        }
        else {
            std::cout << "  (ClaimsAPI not applied; no claims)\n";
        }
    }

    // Re-parse the (already syntactically validated) claims and warn on any
    // capability unknown to the live registry.
    std::vector<std::pair<std::string, std::string>> claims;
    for (const auto& claim : args.claimCapability) {
        std::string capability, degradationClass, error;
        if (!ParseCapabilityClaim(claim, &capability, &degradationClass,
                                  &error)) {
            // Should not happen: validated up front.
            std::cerr << "error: --claim-capability: " << error << "\n";
            return ExitStatus::InvalidArguments;
        }
        if (!UsdProfileRegistry::HasCapability(TfToken(capability))) {
            std::cerr << "warning: capability '" << capability
                      << "' is not known to the registry\n";
        }
        claims.emplace_back(std::move(capability), std::move(degradationClass));
    }

    if (!args.write) {
        if (args.json) {
            JsObject wouldSet;
            for (const auto& c : claims) {
                wouldSet[c.first] = JsValue(c.second);
            }
            jsonReport["wouldSet"] = JsValue(std::move(wouldSet));
            EmitJson(std::cout, std::move(jsonReport));
            return ExitStatus::Success;
        }
        std::cout << "would set (dry-run; pass --write to author):\n";
        for (const auto& c : claims) {
            std::cout << "    " << c.first << " = " << c.second << "\n";
        }
        return ExitStatus::Success;
    }

    // --write: apply the schema, author usages under the resolved edit target,
    // and save.
    bool ok = false;
    UsdEditTarget editTarget = ResolveEditTarget(stage, args, &ok);
    if (!ok) {
        return ExitStatus::InvalidArguments;
    }

    {
        UsdEditContext editContext(stage, editTarget);
        UsdProfilesClaimsAPI api = UsdProfilesClaimsAPI::Apply(prim);
        if (!api) {
            std::cerr << "error: could not apply ClaimsAPI to <"
                      << prim.GetPath() << ">\n";
            return ExitStatus::VerificationFailed;
        }
        for (const auto& c : claims) {
            api.SetCapabilityUsage(TfToken(c.first), TfToken(c.second));
        }
    }

    if (!mark.IsClean()) {
        std::cerr << "error: errors while authoring capability usages:\n";
        for (const TfError& e : mark) {
            std::cerr << "    " << e.GetCommentary() << "\n";
        }
        return ExitStatus::VerificationFailed;
    }

    stage->Save();
    if (!mark.IsClean()) {
        std::cerr << "error: errors while saving stage:\n";
        for (const TfError& e : mark) {
            std::cerr << "    " << e.GetCommentary() << "\n";
        }
        return ExitStatus::VerificationFailed;
    }

    // Re-read to reflect authored state.
    if (args.json) {
        jsonReport["after"] = PrimStateToJs(prim);
        EmitJson(std::cout, std::move(jsonReport));
        return ExitStatus::Success;
    }
    std::cout << "profilesInfo (after) for <" << prim.GetPath() << ">:\n";
    PrintProfilesInfo(std::cout, UsdProfilesClaimsAPI(prim));
    return ExitStatus::Success;
}

// Declare the target prim compatible with a profile, authoring an exception
// set from --except.
ExitStatus RunClaimProfile(const Args& args)
{
    /// @TODO XXX
    (void)args;
    std::cerr << "[stub] --claim-profile not yet implemented\n";
    return ExitStatus::Success;
}

} // anon

int main(int argc, char const *argv[]) {
    CLI::App app(R"(usdprofileclaim -- explicit capability claims, verification, and prim
introspection for OpenUSD.

Authors and verifies profile capability claims on OpenUSD stages and prims,
writing directly into each prim's profilesInfo metadata dictionary. Uses
UsdProfileRegistry and UsdProfilesClaimsAPI to author capability usage,
assert profile compatibility, verify claims against the registered capability
DAG, and deep-introspect prims to infer required capabilities from applied
schemas and layer file formats.

Exactly one mode must be given: --introspect, --verify, --report,
--claim-capability, or --claim-profile.

Capability degradation classes (the CLASS in --claim-capability CAPABILITY=CLASS):
  hard         (or h) Load-bearing; absence causes incorrect rendering or failure.
  soft         (or s) Non-critical; graceful degradation / fallback is supported.
  enhancement  (or e) Quality-of-life; absence is fully allowable.
)");

    app.footer(R"(Exit status:
  0  Success / verification passed.
  1  Verification failed (missing required capability or invalid profile).
  2  Graph conflict detected (deprecation conflict or cycle).
  3  Invalid CLI arguments or path not found.

Examples:
  # Introspect a prim and write inferred capabilities to the stage.
  usdprofileclaim --introspect --prim /World/Assets/Chair --write scene.usda

  # Declare a capability usage on a prim.
  usdprofileclaim --claim-capability usd.geom.hairAndFur=soft --prim /World/Assets/Chair scene.usda

  # Assert profile compatibility with an exception.
  usdprofileclaim --claim-profile profile.studio.vfx.25.08 --except usd.geom.hairAndFur --prim /World/Assets/Chair scene.usda

  # Verify compatibility claims against the registry DAG.
  usdprofileclaim --verify --prim /World/Assets/Chair scene.usda

See also: usdcat(1), usdchecker(1)
)");

    Args args;

    // Mode selection (exactly one required; enforced after parse).
    app.add_flag(
        "-i,--introspect", args.introspect,
        "MODE: deep-inspect the target prim and descendants, inferring "
        "capabilities from applied API schemas and layer file formats");
    app.add_flag(
        "-v,--verify", args.verify,
        "MODE: verify capability usages and profile compatibility claims "
        "against the registry DAG");
    app.add_option(
        "-c,--claim-capability", args.claimCapability,
        "MODE: author a capability usage CAPABILITY=CLASS "
        "(CLASS is hard, soft, or enhancement); repeatable")
        ->type_name("CAPABILITY=CLASS");
    app.add_option(
        "-p,--claim-profile", args.claimProfile,
        "MODE: declare the target prim compatible with PROFILE")
        ->type_name("PROFILE");
    app.add_flag(
        "-R,--report", args.report,
        "MODE: print the target prim's current profile claims (read-only)");

    // Introspection & claim operations.
    app.add_option(
        "-s,--prim", args.prim,
        "Target prim path (SdfPath) within the stage")
        ->type_name("PATH")->capture_default_str();
    app.add_flag(
        "-w,--write", args.write,
        "Write computed/updated claims back to the active edit target");
    app.add_option(
        "-e,--except", args.except,
        "Excepted capability for --claim-profile; repeatable")
        ->type_name("CAPABILITY");
    app.add_option(
        "--pluginfo", args.pluginfo,
        "Use this pluginfo.json instead of the installed capability graph")
        ->type_name("FILE");
    app.add_option(
        "-l,--layer", args.layer,
        "Author into this layer (must be in the stage's local layer stack) "
        "instead of the root layer")
        ->type_name("LAYER");

    // Filtering & output.
    app.add_flag(
        "-j,--json", args.json,
        "Output results in JSON format");
    app.add_flag(
        "--verbose", args.verbose,
        "Print extended diagnostics: path status, version resolution, "
        "DAG reachability");

    // Positional.
    app.add_option(
        "file", args.usdFile, "The target USD file")
        ->required()->type_name("file.usd");

    CLI11_PARSE(app, argc, argv);

    //-------------------------------------------------------------------------
    // Command line validation.

    // Exactly one mode must be selected.
    const int modeCount =
        (args.introspect ? 1 : 0) +
        (args.verify ? 1 : 0) +
        (args.report ? 1 : 0) +
        (args.claimCapability.empty() ? 0 : 1) +
        (args.claimProfile.empty() ? 0 : 1);
    if (modeCount == 0) {
        std::cerr << "error: a mode is required "
                     "(--introspect, --verify, --report, "
                     "--claim-capability, or --claim-profile)\n";
        return static_cast<int>(ExitStatus::InvalidArguments);
    }
    if (modeCount > 1) {
        std::cerr << "error: modes are mutually exclusive; specify exactly "
                     "one of --introspect, --verify, --report, "
                     "--claim-capability, --claim-profile\n";
        return static_cast<int>(ExitStatus::InvalidArguments);
    }

    // --except is only meaningful with --claim-profile.
    if (!args.except.empty() && args.claimProfile.empty()) {
        std::cerr << "error: --except requires --claim-profile\n";
        return static_cast<int>(ExitStatus::InvalidArguments);
    }

    // Validate CAPABILITY=CLASS syntax up front for a clean early exit.
    for (const auto& claim : args.claimCapability) {
        std::string capability, degradationClass, error;
        if (!ParseCapabilityClaim(claim, &capability, &degradationClass,
                                  &error)) {
            std::cerr << "error: --claim-capability: " << error << "\n";
            return static_cast<int>(ExitStatus::InvalidArguments);
        }
    }

    // The target file must exist.
    if (!std::filesystem::exists(args.usdFile)) {
        std::cerr << "error: file not found: " << args.usdFile << "\n";
        return static_cast<int>(ExitStatus::InvalidArguments);
    }

    // ----------------------------------------------------------------------
    // Load graph and dispatch to the selected mode.
    // ----------------------------------------------------------------------
    if (!LoadProfileGraph(args)) {
        return static_cast<int>(ExitStatus::InvalidArguments);
    }

    ExitStatus status = ExitStatus::Success;
    if (args.introspect) {
        status = RunIntrospect(args);
    }
    else if (args.verify) {
        status = RunVerify(args);
    }
    else if (args.report) {
        status = RunReport(std::cout, args);
    }
    else if (!args.claimCapability.empty()) {
        status = RunClaimCapability(args);
    }
    else if (!args.claimProfile.empty()) {
        status = RunClaimProfile(args);
    }

    return static_cast<int>(status);
}
