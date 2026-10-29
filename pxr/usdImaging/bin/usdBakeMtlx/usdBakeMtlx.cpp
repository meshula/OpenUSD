#include "pxr/base/tf/pxrCLI11/CLI11.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/editContext.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/primRange.h"
#include "pxr/usd/usd/relationship.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/variantSets.h"
#include "pxr/usd/usdShade/material.h"
#include "pxr/usd/usdShade/materialBindingAPI.h"

#include "bakeMaterialX.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE
using namespace pxr_CLI;

SdfPath
GetBakedMtlxMaterialPath(const UsdStageRefPtr& usdStage, const UsdShadeMaterial& mtlxMaterial) {
    std::string bakedPrimName = mtlxMaterial.GetPath().GetName() + "_baked";
    SdfPath bakedMaterialPath = mtlxMaterial.GetPrim().GetParent().GetPath().AppendChild(TfToken(bakedPrimName));

    if (!usdStage->GetPrimAtPath(bakedMaterialPath)) {
        std::cerr << "Invalid material at path " << bakedMaterialPath << std::endl;
        exit(1);
    }
    return bakedMaterialPath;
}

std::vector<UsdShadeMaterial>
GetMaterialXMaterials(const UsdStageRefPtr& usdStage) {
    std::vector<UsdShadeMaterial> mtlxMaterials;
    for (auto prim : usdStage->Traverse()) {
        auto material = UsdShadeMaterial(prim);
        if (!material) {
            continue;
        }
        auto outputs = material.GetOutputs();
        for (const auto& output : outputs) {
            if (output.GetBaseName() == "mtlx:surface") {
                mtlxMaterials.push_back(material);
                break;
            }
        }
    }
    return mtlxMaterials;
}

std::vector<std::pair<UsdPrim, UsdRelationship>>
GetMtlxBoundPrims(const UsdStageRefPtr& usdStage, const SdfPath& materialPath) {
    std::vector<std::pair<UsdPrim, UsdRelationship>> results;
    for (auto prim : usdStage->Traverse()) {
        UsdShadeMaterialBindingAPI bindingAPI(prim);
        if (!bindingAPI) {
            continue;
        }

        UsdRelationship directBindingRel = bindingAPI.GetDirectBindingRel();
        if (directBindingRel) {
            SdfPathVector targets;
            directBindingRel.GetTargets(&targets);
            for (const auto& target : targets) {
                if (target == materialPath) {
                    results.emplace_back(prim, directBindingRel);
                    break;
                }
            }
        }
    }
    return results;
}

int
main(int argc, char const *argv[])
{
    std::string progName = TfGetBaseName(argv[0]);
    CLI::App app("Output an updated USD file that contains an additional material variant "
                 "corresponding to a baked version of each MaterialX material in the input USD "
                 "file. The baked materials are generated using the MaterialX::TextureBaker "
                 "class and will generate a mtlx file and associated texture(s).",
                 progName);

    int width = 1024;
    app.add_option("--width", width, "Specify the width of the baked textures.")
        ->default_val(width);

    int height = 1024;
    app.add_option("--height", height, "Specify the height of the baked textures.")
        ->default_val(height);

    bool hdr = false;
    app.add_flag("--hdr", hdr, "Save images to hdr format.");

    bool average = false;
    app.add_flag("--average", average, "Average baked images to generate constant values.");

    std::string inputUsdFilename, outputUsdFilename;
    app.add_option("inputUsdFilename", inputUsdFilename, "Name of the input Usd file")
        ->required(true);
    app.add_option("outputUsdFilename", outputUsdFilename,
                   "Name of the output Usd File, the generated mtlx File and any generated images, "
                   "will be saved to this directory.")
        ->required(true);

    CLI11_PARSE(app, argc, argv);

    std::string outputDir = std::filesystem::path(outputUsdFilename).parent_path().string();
    if (outputDir.empty()) {
        outputDir = ".";
    }

    UsdStageRefPtr usdStage = UsdStage::Open(inputUsdFilename);
    if (!usdStage) {
        std::cerr << "ERROR: Could not open layer" << std::endl;
        exit(1);
    }

    auto mtlxMaterials = GetMaterialXMaterials(usdStage);
    if (mtlxMaterials.empty()) {
        std::cerr << "No MaterialX Materials detected in the given USD File" << std::endl;
        exit(1);
    }

    for (const auto& mtlxMaterial : mtlxMaterials) {
        // Translate the UsdShade mtlxMaterial into a MaterialX Document and Bake
        std::string bakedFileName = UsdBakeMtlxBakeMaterial(
            mtlxMaterial,
            outputDir,
            width, height,
            hdr, average);

        if (bakedFileName.empty()) {
            continue;
        }

        // Read the Baked MaterialX Document into the existing USD Stage
        UsdBakeMtlxReadDocToStage(bakedFileName, usdStage);

        // Add Variants to the Prims with Bindings to the mtlxMaterial
        SdfPath bakedMaterialPath = GetBakedMtlxMaterialPath(usdStage, mtlxMaterial);
        auto needsVariant = GetMtlxBoundPrims(usdStage, mtlxMaterial.GetPath());

        for (auto& [prim, bindingRel] : needsVariant) {
            TfToken relName = bindingRel.GetName();
            prim.RemoveProperty(relName);

            UsdPrim rootPrim = prim.GetParent();
            if (!rootPrim.IsValid() ||
                !(rootPrim.GetPath().IsPrimPath() || rootPrim.GetPath().IsPrimVariantSelectionPath())) {
                rootPrim = prim;
            }

            UsdVariantSet vset = rootPrim.GetVariantSets().AddVariantSet("MtlxShadingVariant");
            vset.AddVariant("original");
            vset.AddVariant("baked");

            // Write targets for "original" variant
            vset.SetVariantSelection("original");
            {
                UsdEditContext editContext(vset.GetVariantEditContext());
                UsdRelationship varBindingRel = prim.CreateRelationship(relName, /* custom = */ false);
                varBindingRel.SetTargets({mtlxMaterial.GetPath()});
            }

            // Write targets for "baked" variant
            vset.SetVariantSelection("baked");
            {
                UsdEditContext editContext(vset.GetVariantEditContext());
                UsdRelationship varBindingRel = prim.CreateRelationship(relName, /* custom = */ false);
                varBindingRel.SetTargets({bakedMaterialPath});
            }
        }
    }

    usdStage->GetRootLayer()->Export(outputUsdFilename);
    std::cout << "\nWrote baked USD file: " << outputUsdFilename << std::endl;
    return 0;
}