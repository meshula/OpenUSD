
//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "pxr/pxr.h"
#include "pxr/usd/usdProfiles/profileRegistry.h"

#include <cstdio>
#include <string>

PXR_NAMESPACE_USING_DIRECTIVE

static void
TestLoadCustomProfileData()
{
    std::ifstream ifs("testCustomDomain.json");
    TF_AXIOM(ifs.is_open());
    JsParseError parseError;
    JsValue root = JsParseStream(ifs, &parseError);
    TF_AXIOM(!root.IsNull() && root.IsObject());
    const JsObject& rootObj = root.GetJsObject();
    JsObject profileData;
    TF_AXIOM(_GetKey(infoDict, _tokens->PluginProfilesKey, &profileData));
    instance.MergeProfileData(profileData);

    // Validate the graph
    std::vector<std::string> validationErrors;
    TF_AXIOM(instance._capabilityGraph->ValidateEdges(&validationErrors));
    TF_AXIOM(instance._capabilityGraph->ValidateAcyclic(&validationErrors));
    TF_AXIOM(instance._capabilityGraph->ValidateRoot(&validationErrors));

    UsdProfilesClaimsAPI api;
    UsdStageRefPtr stage = _MakeStage(&api);

    printf("  TestLoadCustomProfileData passed\n");
}

//----------------------------------------------------------------------------
// main
//----------------------------------------------------------------------------

int
main(int argc, char* argv[])
{
    printf("testCustomGraph\n");

    TestLoadCustomProfileData();
    printf("All tests passed\n");
    return 0;
}
