//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "pxr/pxr.h"
#include "pxr/usd/usdProfiles/claimsAPI.h"
#include "pxr/usd/usdProfiles/profileRegistry.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/base/js/json.h"
#include "pxr/base/js/value.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/vt/dictionary.h"

#include <cstdio>
#include <fstream>
#include <set>
#include <string>

PXR_NAMESPACE_USING_DIRECTIVE

// Exercises a custom, non-usd capability domain (the "space race" tech tree
// in testCustomDomain.json) merged into the registry alongside the usd graph.

using CR = UsdProfileRegistry::CapabilityResult;
using QS = UsdProfileRegistry::QueryStatus;

//----------------------------------------------------------------------------
// Helpers
//----------------------------------------------------------------------------

// Load testCustomDomain.json and merge its Profiles block into the registry.
static void
_LoadCustomDomain()
{
    std::ifstream ifs("testCustomDomain.json");
    TF_AXIOM(ifs.is_open());

    JsParseError parseError;
    JsValue root = JsParseStream(ifs, &parseError);
    TF_AXIOM(!root.IsNull() && root.IsObject());

    const JsObject& rootObj = root.GetJsObject();
    auto it = rootObj.find("Profiles");
    TF_AXIOM(it != rootObj.end() && it->second.IsObject());

    TF_AXIOM(TfSingleton<UsdProfileRegistry>::GetInstance()
                 .MergeProfileData(it->second.GetJsObject()));
}

static UsdStageRefPtr
_OpenStage(const std::string& usda)
{
    UsdStageRefPtr stage = UsdStage::CreateInMemory("tmp.usda");
    TF_AXIOM(stage->GetRootLayer()->ImportFromString(usda));
    return stage;
}

// Return the tokens in \p results whose status is \c NoPath.
static std::set<TfToken>
_Missing(const std::set<CR>& results)
{
    std::set<TfToken> missing;
    for (const CR& r : results) {
        if (r.status == QS::NoPath) {
            missing.insert(r.capability);
        }
    }
    return missing;
}

static void
_Report(const UsdPrim& prim, const char* yes, const char* no,
        const TfToken& target, bool ok, const std::set<TfToken>& problems,
        const char* problemLabel)
{
    printf("    %s %s %s", prim.GetPath().GetText(),
           ok ? yes : no, target.GetText());
    if (!problems.empty()) {
        printf(" (%s:", problemLabel);
        for (const TfToken& m : problems) {
            printf(" %s", m.GetText());
        }
        printf(")");
    }
    printf("\n");
}

// Check whether \p prim provides everything \p target requires, report it,
// and return the missing requirements.
static std::set<TfToken>
_CheckSatisfies(const UsdPrim& prim, const TfToken& target, bool expected)
{
    UsdProfilesClaimsAPI api(prim);
    TF_AXIOM(api);

    std::set<CR> results;
    QS status = api.Satisfies(target, &results);
    bool ok = status == QS::ValidPath;
    std::set<TfToken> missing = _Missing(results);
    _Report(prim, "satisfies", "does NOT satisfy", target, ok, missing,
            "missing");

    TF_AXIOM(ok == expected);
    TF_AXIOM(ok == missing.empty());
    return missing;
}

static const char* _spaceRaceLayer = R"(#usda 1.0

def "Orbiter" (
    prepend apiSchemas = ["ClaimsAPI"]
    profiles = {
        dictionary capabilityUsages = {
            string "techtree" = "hard"
            string "techtree.space" = "hard"
            string "techtree.space.sounding" = "hard"
            string "techtree.space.orbital" = "hard"
        }
    }
)
{
}

def "MannedOrbiter" (
    prepend apiSchemas = ["ClaimsAPI"]
    profiles = {
        dictionary capabilityUsages = {
            string "techtree" = "hard"
            string "techtree.space" = "hard"
            string "techtree.space.sounding" = "hard"
            string "techtree.space.orbital" = "hard"
            string "techtree.space.lifesupport" = "hard"
            string "techtree.space.manned" = "hard"
        }
    }
)
{
}

def "LunarLander" (
    prepend apiSchemas = ["ClaimsAPI"]
    profiles = {
        dictionary capabilityUsages = {
            string "techtree" = "hard"
            string "techtree.space" = "hard"
            string "techtree.space.sounding" = "hard"
            string "techtree.space.orbital" = "hard"
            string "techtree.space.lifesupport" = "hard"
            string "techtree.space.manned" = "hard"
            string "techtree.space.rendezvous" = "hard"
            string "techtree.space.lunar" = "hard"
        }
    }
)
{
}

def "LunarLanderNoLifeSupport" (
    prepend apiSchemas = ["ClaimsAPI"]
    profiles = {
        dictionary capabilityUsages = {
            string "techtree" = "hard"
            string "techtree.space" = "hard"
            string "techtree.space.sounding" = "hard"
            string "techtree.space.orbital" = "hard"
            string "techtree.space.manned" = "hard"
            string "techtree.space.rendezvous" = "hard"
            string "techtree.space.lunar" = "hard"
        }
    }
)
{
}
)";

//----------------------------------------------------------------------------
// (1) The space race DAG is loaded into the registry
//----------------------------------------------------------------------------

static void
TestCustomGraphLoaded()
{
    const char* caps[] = {
        "techtree",
        "techtree.space",
        "techtree.space.sounding",
        "techtree.space.orbital",
        "techtree.space.lifesupport",
        "techtree.space.manned",
        "techtree.space.rendezvous",
        "techtree.space.lunar",
        "techtree.profile.mannedOrbital",
        "techtree.profile.lunarLanding",
    };
    for (const char* c : caps) {
        TF_AXIOM(UsdProfileRegistry::HasCapability(TfToken(c)));
    }

    // The custom domain merges alongside, not instead of, the usd graph.
    TF_AXIOM(UsdProfileRegistry::HasCapability(TfToken("usd")));

    TF_AXIOM(UsdProfileRegistry::IsProfile(
        TfToken("techtree.profile.mannedOrbital")));
    TF_AXIOM(UsdProfileRegistry::IsProfile(
        TfToken("techtree.profile.lunarLanding")));
    TF_AXIOM(!UsdProfileRegistry::IsProfile(TfToken("techtree.space.lunar")));

    // Edges: the lunar node joins the two "diamond" branches.
    std::set<TfToken> lunarPreds = UsdProfileRegistry::CapabilityResultsToTokens(
        UsdProfileRegistry::GetPredecessors(TfToken("techtree.space.lunar")));
    TF_AXIOM(lunarPreds == std::set<TfToken>({
        TfToken("techtree.space.manned"),
        TfToken("techtree.space.rendezvous")}));

    // Metadata and styles came along too.
    TF_AXIOM(UsdProfileRegistry::GetDisplayName(
        TfToken("techtree.space.lunar")) == "Moon Landing");
    TF_AXIOM(UsdProfileRegistry::GetStyleForCapability(
        TfToken("techtree.space.lunar")) == TfToken("apex"));
    TF_AXIOM(UsdProfileRegistry::GetCapabilityStyles().count(TfToken("apex")));

    printf("  TestCustomGraphLoaded passed\n");
}

//----------------------------------------------------------------------------
// (2) The custom domain has its own root, disconnected from "usd"
//----------------------------------------------------------------------------

static void
TestCustomGraphOwnRoot()
{
    const TfToken usdRoot("usd");
    const TfToken techtree("techtree");

    // techtree is a root.
    TF_AXIOM(UsdProfileRegistry::GetPredecessors(techtree).empty());

    // Every node in the domain reaches techtree and never reaches usd.
    for (const TfToken& cap : UsdProfileRegistry::GetAllCapabilities()) {
        if (!TfStringStartsWith(cap.GetString(), "techtree.")) {
            continue;
        }
        std::set<TfToken> ancestors =
            UsdProfileRegistry::CapabilityResultsToTokens(
                UsdProfileRegistry::GetTransitivePredecessors(cap));
        TF_AXIOM(ancestors.count(techtree));
        TF_AXIOM(!ancestors.count(usdRoot));
        TF_AXIOM(UsdProfileRegistry::HasPredecessor(cap, usdRoot)
                 == QS::NoPath);
    }

    // And nothing in the usd graph reaches into the custom domain.
    TF_AXIOM(UsdProfileRegistry::HasPredecessor(usdRoot, techtree)
             == QS::NoPath);

    printf("  TestCustomGraphOwnRoot passed\n");
}

//----------------------------------------------------------------------------
// (3) A prim with everything needed for orbital flight
//----------------------------------------------------------------------------

static void
TestOrbitalPrim()
{
    UsdStageRefPtr stage = _OpenStage(_spaceRaceLayer);
    UsdPrim orbiter = stage->GetPrimAtPath(SdfPath("/Orbiter"));
    TF_AXIOM(orbiter && orbiter.HasAPI<UsdProfilesClaimsAPI>());

    _CheckSatisfies(orbiter, TfToken("techtree.space.orbital"), true);

    // Orbit alone isn't enough to put a crew up there.
    const TfToken manned("techtree.space.manned");
    std::set<TfToken> missing = _CheckSatisfies(orbiter, manned, false);
    TF_AXIOM(missing == std::set<TfToken>({
        TfToken("techtree.space.lifesupport"), manned}));

    // The registry-level query gives the same answer from a plain token set.
    std::set<CR> results;
    TF_AXIOM(UsdProfileRegistry::SatisfiesCapabilities(
                 TfToken("techtree.space.orbital"),
                 { TfToken("techtree"), TfToken("techtree.space"),
                   TfToken("techtree.space.sounding"),
                   TfToken("techtree.space.orbital") },
                 &results) == QS::ValidPath);
    TF_AXIOM(results.size() == 4);

    printf("  TestOrbitalPrim passed\n");
}

//----------------------------------------------------------------------------
// (4) Everything for the Moon except life support is not enough
//----------------------------------------------------------------------------

static void
TestLunarWithoutLifeSupport()
{
    UsdStageRefPtr stage = _OpenStage(_spaceRaceLayer);
    UsdPrim lander = stage->GetPrimAtPath(SdfPath("/LunarLanderNoLifeSupport"));
    TF_AXIOM(lander);

    std::set<TfToken> missing =
        _CheckSatisfies(lander, TfToken("techtree.space.lunar"), false);
    TF_AXIOM(missing == std::set<TfToken>({
        TfToken("techtree.space.lifesupport")}));

    printf("  TestLunarWithoutLifeSupport passed\n");
}

//----------------------------------------------------------------------------
// (5) Manned orbital and lunar landing profiles
//----------------------------------------------------------------------------

struct _ProfileExpectation {
    const char* path;
    bool mannedOrbital;
    bool lunarLanding;
};

// Producer side: does the vehicle have every prerequisite of the profile?
static void
TestProfilesSatisfied()
{
    UsdStageRefPtr stage = _OpenStage(_spaceRaceLayer);

    const TfToken mannedOrbital("techtree.profile.mannedOrbital");
    const TfToken lunarLanding("techtree.profile.lunarLanding");

    const _ProfileExpectation expectations[] = {
        { "/Orbiter",                  false, false },
        { "/MannedOrbiter",            true,  false },
        { "/LunarLander",              true,  true  },
        { "/LunarLanderNoLifeSupport", false, false },
    };

    for (const _ProfileExpectation& e : expectations) {
        UsdPrim prim = stage->GetPrimAtPath(SdfPath(e.path));
        TF_AXIOM(prim);
        _CheckSatisfies(prim, mannedOrbital, e.mannedOrbital);
        _CheckSatisfies(prim, lunarLanding, e.lunarLanding);
    }

    printf("  TestProfilesSatisfied passed\n");
}

// Consumer side: read each profile as a mission control rated for that kind
// of mission. Does the profile support everything the vehicle uses? This is
// the CoversCapabilities / IsCompatibleWith direction. It is the set of
// answers opposite of the above: a lunar-rated control supports a plain 
// orbiter, but a manned-orbital-rated control can't fly a lander.
static void
TestProfilesCompatible()
{
    UsdStageRefPtr stage = _OpenStage(_spaceRaceLayer);

    const TfToken mannedOrbital("techtree.profile.mannedOrbital");
    const TfToken lunarLanding("techtree.profile.lunarLanding");

    const _ProfileExpectation expectations[] = {
        { "/Orbiter",                  true,  true  },
        { "/MannedOrbiter",            true,  true  },
        { "/LunarLander",              false, true  },
        { "/LunarLanderNoLifeSupport", false, true  },
    };

    for (const _ProfileExpectation& e : expectations) {
        UsdPrim prim = stage->GetPrimAtPath(SdfPath(e.path));
        UsdProfilesClaimsAPI api(prim);
        TF_AXIOM(api);

        // IsCompatibleWith only evaluates profiles the prim claims.
        TF_AXIOM(api.IsCompatibleWith(mannedOrbital) == QS::NoPath);
        api.SetProfileCompatible(mannedOrbital);
        api.SetProfileCompatible(lunarLanding);

        for (const auto& [profile, expected] : {
                 std::make_pair(mannedOrbital, e.mannedOrbital),
                 std::make_pair(lunarLanding, e.lunarLanding) }) {
            std::set<CR> results;
            bool ok = api.IsCompatibleWith(profile, &results) == QS::ValidPath;
            _Report(prim, "is compatible with", "is NOT compatible with",
                    profile, ok, _Missing(results), "unsupported");
            TF_AXIOM(ok == expected);
        }
    }

    // Record that the manned-orbital control doesn't support the deep space
    // work. Those capabilities report Excepted rather than NoPath.
    UsdProfilesClaimsAPI lander(stage->GetPrimAtPath(SdfPath("/LunarLander")));
    lander.SetProfileCompatibleWithExceptions(mannedOrbital, {
        TfToken("techtree.space.rendezvous"), TfToken("techtree.space.lunar") });
    std::set<CR> results;

    // By default an exception doesn't excuse anything: still not compatible.
    TF_AXIOM(lander.IsCompatibleWith(mannedOrbital, &results) == QS::NoPath);
    TF_AXIOM(_Missing(results).empty());
    for (const CR& r : results) {
        TF_AXIOM(r.status == QS::ValidPath || r.status == QS::Excepted);
    }

    // Opting in excuses them. Test that allowing excuses passes.
    results.clear();
    TF_AXIOM(lander.IsCompatibleWith(mannedOrbital, &results,
                                     /* allowExceptions */ true)
             == QS::Excepted);
    _Report(lander.GetPrim(), "is compatible (with exceptions) with",
            "is NOT compatible with", mannedOrbital, true, {}, "");

    // Excusing only part of what's unsupported still fails.
    lander.SetProfileCompatibleWithExceptions(mannedOrbital, {
        TfToken("techtree.space.lunar") });
    TF_AXIOM(lander.IsCompatibleWith(mannedOrbital, nullptr, true)
             == QS::NoPath);

    // A prim that needs no exceptions still reports ValidPath when opted in.
    UsdProfilesClaimsAPI orbiter(stage->GetPrimAtPath(SdfPath("/Orbiter")));
    TF_AXIOM(orbiter.IsCompatibleWith(mannedOrbital, nullptr, true)
             == QS::ValidPath);

    // The registry-level query gives the same answer from a plain token set.
    TF_AXIOM(UsdProfileRegistry::CoversCapabilities(
                 lunarLanding,
                 { TfToken("techtree.space.rendezvous"),
                   TfToken("techtree.space.lunar") }) == QS::ValidPath);
    TF_AXIOM(UsdProfileRegistry::CoversCapabilities(
                 mannedOrbital,
                 { TfToken("techtree.space.rendezvous") }) == QS::NoPath);

    printf("  TestProfilesCompatible passed\n");
}

//----------------------------------------------------------------------------
// main
//----------------------------------------------------------------------------

int
main(int argc, char* argv[])
{
    printf("testCustomGraph\n");

    _LoadCustomDomain();

    TestCustomGraphLoaded();
    TestCustomGraphOwnRoot();
    TestOrbitalPrim();
    TestLunarWithoutLifeSupport();
    TestProfilesSatisfied();
    TestProfilesCompatible();

    printf("All tests passed\n");
    return 0;
}
