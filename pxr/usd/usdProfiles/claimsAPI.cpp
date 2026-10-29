//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/usd/usdProfiles/claimsAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<UsdProfilesClaimsAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

/* virtual */
UsdProfilesClaimsAPI::~UsdProfilesClaimsAPI()
{
}

/* static */
UsdProfilesClaimsAPI
UsdProfilesClaimsAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdProfilesClaimsAPI();
    }
    return UsdProfilesClaimsAPI(stage->GetPrimAtPath(path));
}


/* virtual */
UsdSchemaKind UsdProfilesClaimsAPI::_GetSchemaKind() const
{
    return UsdProfilesClaimsAPI::schemaKind;
}

/* static */
bool
UsdProfilesClaimsAPI::CanApply(
    const UsdPrim &prim, std::string *whyNot)
{
    return prim.CanApplyAPI<UsdProfilesClaimsAPI>(whyNot);
}

/* static */
UsdProfilesClaimsAPI
UsdProfilesClaimsAPI::Apply(const UsdPrim &prim)
{
    if (prim.ApplyAPI<UsdProfilesClaimsAPI>()) {
        return UsdProfilesClaimsAPI(prim);
    }
    return UsdProfilesClaimsAPI();
}

/* static */
const TfType &
UsdProfilesClaimsAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdProfilesClaimsAPI>();
    return tfType;
}

/* static */
bool 
UsdProfilesClaimsAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdProfilesClaimsAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

/*static*/
const TfTokenVector&
UsdProfilesClaimsAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames;
    static TfTokenVector allNames =
        UsdAPISchemaBase::GetSchemaAttributeNames(true);

    if (includeInherited)
        return allNames;
    else
        return localNames;
}

PXR_NAMESPACE_CLOSE_SCOPE

// ===================================================================== //
// Feel free to add custom code below this line. It will be preserved by
// the code generator.
//
// Just remember to wrap code in the appropriate delimiters:
// 'PXR_NAMESPACE_OPEN_SCOPE', 'PXR_NAMESPACE_CLOSE_SCOPE'.
// ===================================================================== //
// --(BEGIN CUSTOM CODE)--

#include "pxr/usd/usdProfiles/profileRegistry.h"
#include "pxr/usd/usdProfiles/tokens.h"
#include "pxr/usd/usd/primRange.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/sdf/fileFormat.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/base/tf/hash.h"
#include "pxr/base/tf/type.h"
#include "pxr/base/vt/array.h"
#include "pxr/base/vt/dictionary.h"

#include <unordered_map>
#include <unordered_set>

PXR_NAMESPACE_OPEN_SCOPE

// Static key paths for the two top-level sub-dictionaries.
// Dynamic leaf paths (e.g. per-capability) are built by _ExtendKey at runtime.
TF_DEFINE_PRIVATE_TOKENS(
    _profilesTokens,
    (profiles)
    (capabilityUsages)
    (profileCompatibility)
    ((capabilityUsagesKey,        "capabilityUsages"))
    ((profileCompatibilityKey,    "profileCompatibility"))
);

// Append a dynamic leaf token to a static prefix.
// e.g. _ExtendKey(capabilityUsagesKey, "usd.geom.mesh")
//      -> "profilesInfo:capabilityUsages:usd.geom.mesh"
static TfToken
_ExtendKey(const TfToken &prefix, const TfToken &leaf)
{
    return TfToken(prefix.GetString() + ":" + leaf.GetString());
}

// ---------------------------------------------------------------------- //
// _GetProfilesInfoByKey
// ---------------------------------------------------------------------- //

template <typename T>
bool
UsdProfilesClaimsAPI::_GetProfilesInfoByKey(const TfToken &keyPath, T *val) const
{
    VtValue v;
    if (!GetPrim().GetMetadataByDictKey(
            _profilesTokens->profiles, keyPath, &v)) {
        return false;
    }
    if (!v.IsHolding<T>()) {
        return false;
    }
    *val = v.UncheckedGet<T>();
    return true;
}

// ---------------------------------------------------------------------- //
// Raw dictionary access
// ---------------------------------------------------------------------- //

VtDictionary
UsdProfilesClaimsAPI::GetProfilesInfo() const
{
    VtDictionary result;
    GetPrim().GetMetadata(_profilesTokens->profiles, &result);
    return result;
}

void
UsdProfilesClaimsAPI::SetProfilesInfo(const VtDictionary &info) const
{
    GetPrim().SetMetadata(_profilesTokens->profiles, info);
}

// ---------------------------------------------------------------------- //
// Capability Usage
// ---------------------------------------------------------------------- //

VtDictionary
UsdProfilesClaimsAPI::GetCapabilityUsages() const
{
    VtDictionary result;
    _GetProfilesInfoByKey(_profilesTokens->capabilityUsagesKey, &result);
    return result;
}

void
UsdProfilesClaimsAPI::SetCapabilityUsages(const VtDictionary &usages) const
{
    GetPrim().SetMetadataByDictKey(
        _profilesTokens->profiles,
        _profilesTokens->capabilityUsagesKey,
        usages);
}

void
UsdProfilesClaimsAPI::SetCapabilityUsage(const TfToken &capability,
                                   const TfToken &degradationClass) const
{
    GetPrim().SetMetadataByDictKey(
        _profilesTokens->profiles,
        _ExtendKey(_profilesTokens->capabilityUsagesKey, capability),
        VtValue(degradationClass));
}

TfToken
UsdProfilesClaimsAPI::GetCapabilityUsage(const TfToken &capability) const
{
    TfToken result;
    _GetProfilesInfoByKey(
        _ExtendKey(_profilesTokens->capabilityUsagesKey, capability), &result);
    return result;
}

// ---------------------------------------------------------------------- //
// Profile Compatibility
// ---------------------------------------------------------------------- //

VtArray<TfToken>
UsdProfilesClaimsAPI::GetCompatibleProfiles() const
{
    VtDictionary compat;
    if (!_GetProfilesInfoByKey(
            _profilesTokens->profileCompatibilityKey, &compat)) {
        return {};
    }
    VtArray<TfToken> profiles;
    profiles.reserve(compat.size());
    for (const auto &entry : compat) {
        profiles.push_back(TfToken(entry.first));
    }
    return profiles;
}

void
UsdProfilesClaimsAPI::SetProfileCompatible(const TfToken &profile) const
{
    GetPrim().SetMetadataByDictKey(
        _profilesTokens->profiles,
        _ExtendKey(_profilesTokens->profileCompatibilityKey, profile),
        VtValue(VtArray<TfToken>()));
}

void
UsdProfilesClaimsAPI::SetProfileCompatibleWithExceptions(
    const TfToken &profile,
    const VtArray<TfToken> &exceptions) const
{
    GetPrim().SetMetadataByDictKey(
        _profilesTokens->profiles,
        _ExtendKey(_profilesTokens->profileCompatibilityKey, profile),
        VtValue(exceptions));
}

VtArray<TfToken>
UsdProfilesClaimsAPI::GetProfileExceptions(const TfToken &profile) const
{
    VtArray<TfToken> result;
    _GetProfilesInfoByKey(
        _ExtendKey(_profilesTokens->profileCompatibilityKey, profile), &result);
    return result;
}

void
UsdProfilesClaimsAPI::ClearProfileCompatibility(const TfToken &profile) const
{
    GetPrim().ClearMetadataByDictKey(
        _profilesTokens->profiles,
        _ExtendKey(_profilesTokens->profileCompatibilityKey, profile));
}

// ---------------------------------------------------------------------- //
// IsCompatibleWith
// ---------------------------------------------------------------------- //

// Collects implied capability names (no strength) from every applied schema on
// the prim and every distinct file format among the stage's used layers.
// Uses std::set internally for dedup across the walk.
static std::set<TfToken>
_GatherImpliedCapabilities(const UsdPrim& rootPrim)
{
    std::set<TfToken> implied;
    if (!rootPrim) {
        return implied;
    }

    UsdProfileRegistry::TypeCapabilityCache typeCache;
    std::unordered_set<UsdPrim, TfHash> visitedPrototypes;
    std::vector<UsdPrim> prototypeStack;

    // Helper to queue unique prototypes
    auto queuePrototype = [&](const UsdPrim& proto) {
        if (proto && visitedPrototypes.insert(proto).second) {
            prototypeStack.push_back(proto);
        }
    };

    // Helper to inspect a prim and record implied capabilities
    auto inspectPrim = [&](const UsdPrim& subtreeRoot) {
        std::set<TfToken> primCaps =
            UsdProfileRegistry::GetPrimImpliedCapabilities(subtreeRoot, nullptr);// @dp &typeCache);
        implied.insert(primCaps.begin(), primCaps.end());
    };

    // Helper to traverse the main scene graph (rootPrim and all descendant
    // un-instanced prims).
    auto inspectPrimRange = [&](const UsdPrim& traversePrim) {
        for (const UsdPrim& prim : UsdPrimRange(traversePrim)) {
            inspectPrim(prim);

            // If this prim is an instance root, record its prototype for traversal
            if (prim.IsInstance()) {
                if (UsdPrim proto = prim.GetPrototype()) {
                    queuePrototype(proto);
                }
            }
        }
    };

    // Edge case: If rootPrim itself is an instance or proxy, queue its prototype
    if (rootPrim.IsInstance()) {
        queuePrototype(rootPrim.GetPrototype());
    }

    inspectPrimRange(rootPrim);

    // Walk only the prototypes referenced within rootPrim's subtree.
    while (!prototypeStack.empty()) {
        const UsdPrim protoRoot = prototypeStack.back();
        prototypeStack.pop_back();
        inspectPrimRange(protoRoot);
    }

    return implied;
}

VtDictionary
UsdProfilesClaimsAPI::ComputeCapabilityUsages() const
{
    UsdPrim rootPrim = GetPrim();
    if (!rootPrim) {
        return VtDictionary();
    }

    // Start from whatever the user has already authored on this prim.
    // Authored strengths are load-bearing: the user has explicitly declared
    // how the application consumes the capability ("enhancement", "soft", or
    // "hard"). These survive verbatim.
    VtDictionary merged = GetCapabilityUsages();

    // Fill in implied capabilities that the user has not declared. Default
    // strength for an undeclared implication is "hard".
    for (const TfToken& cap : _GatherImpliedCapabilities(rootPrim)) {
        const std::string capStr = cap.GetString();
        if (merged.find(capStr) == merged.end()) {
            merged[capStr] = VtValue(UsdProfilesTokens->hard);
        }
    }

    return merged;
}

VtDictionary
UsdProfilesClaimsAPI::PopulateCapabilityUsages() const
{
    UsdPrim rootPrim = GetPrim();
    if (!rootPrim) {
        return VtDictionary();
    }

    VtDictionary merged = ComputeCapabilityUsages();
    if (!rootPrim.IsInstance()) {
        SetCapabilityUsages(merged);
    }
    else {
        TF_WARN("Cannot populate capability usages on an instance %s",
                rootPrim.GetName().GetString().c_str());
    }
    return merged;
}

UsdProfileRegistry::QueryStatus
UsdProfilesClaimsAPI::IsCompatibleWith(
    const TfToken &profile,
    std::set<UsdProfileRegistry::CapabilityResult> *results) const
{
    // A compatibility entry must exist for this profile.
    VtDictionary compat;
    if (!_GetProfilesInfoByKey(
            _profilesTokens->profileCompatibilityKey, &compat)
        || compat.find(profile.GetString()) == compat.end()) {
        return UsdProfileRegistry::QueryStatus::NoPath;
    }

    // Collect required capabilities from the usage map.
    VtDictionary usages = GetCapabilityUsages();
    if (usages.empty()) {
        return UsdProfileRegistry::QueryStatus::NoPath;
    }
    std::set<TfToken> required;
    for (const auto &entry : usages) {
        required.insert(TfToken(entry.first));
    }

    // Pass the stored exception set for this profile.
    VtArray<TfToken> exceptions = GetProfileExceptions(profile);
    std::set<TfToken> excepted(exceptions.begin(), exceptions.end());

    return UsdProfileRegistry::CoversCapabilities(
        profile, required, excepted, results);
}

PXR_NAMESPACE_CLOSE_SCOPE
