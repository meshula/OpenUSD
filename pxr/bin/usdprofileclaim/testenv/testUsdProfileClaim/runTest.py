#!/pxrpythonsubst
#
# Copyright 2026 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.

import json
import os
import shutil
import subprocess
import sys
import tempfile

# The capability tokens used in this test are registered tokens from the live
# UsdProfiles graph (pxr/usd/usd/plugInfo.json).

CAPABILITY = "usd.core.colorspace"
DEGRADATION = "hard"
PRIM = "/FurryCube"

# usdprofileclaim is on PATH via the test's PRE_PATH. Resolve the fixture
# relative to this script (the testenv is copied alongside it).
BINARY = "usdprofileclaim"
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FIXTURE_SRC = os.path.join(SCRIPT_DIR, "furryCube.usda")

# The procedural city fixture (see genCity.py). city.usda references
# building.usda, which references cityMaterial.mtlx; all three must be copied
# into the workdir together so the references resolve.
CITY_FIXTURES = ("city.usda", "building.usda", "cityMaterial.mtlx")

# Inferred capability tokens. colorspace is always available in the live graph.
# The .mtlx file format inference depends on usdMtlx plugin discovery in the
# ctest env - is it asserted only if the plugin is available.
COLORSPACE = "usd.core.colorspace"
MTLX = "usd.mtlx.fileformat"


def run(args):
    """Run usdprofileclaim with the given args, returning (rc, stdout)."""
    cmd = [BINARY] + args
    print("+ " + " ".join(cmd))
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          universal_newlines=True)
    sys.stdout.write(proc.stdout)
    return proc.returncode, proc.stdout


def main():
    # Work in a temp copy so --write does not dirty the source tree.
    workdir = tempfile.mkdtemp(prefix="testUsdProfileClaim_")
    try:
        usdFile = os.path.join(workdir, "furryCube.usda")
        shutil.copyfile(FIXTURE_SRC, usdFile)

        #--------------------------------------------------------------
        # Test 1. Report on the pristine fixture.
        rc, out = run(["--report", "--prim", PRIM, usdFile])
        assert rc == 0, "report failed with rc %d" % rc
        assert "ClaimsAPI not applied" in out or "(none)" in out, \
            "expected empty claims in report output:\n%s" % out

        #--------------------------------------------------------------
        # Test 2. Dry-run a claim.
        before = open(usdFile, "rb").read()
        rc, out = run(["--claim-capability", "%s=%s" % (CAPABILITY, DEGRADATION),
                       "--prim", PRIM, usdFile])
        assert rc == 0, "dry-run claim failed with rc %d" % rc
        assert "would set" in out, \
            "expected 'would set' in dry-run output:\n%s" % out
        after = open(usdFile, "rb").read()
        assert before == after, "dry-run must not modify the file"

        #--------------------------------------------------------------
        # Test 3. Write a claim ot the temp copy.
        rc, out = run(["--claim-capability", "%s=%s" % (CAPABILITY, DEGRADATION),
                       "--prim", PRIM, "--write", usdFile])
        assert rc == 0, "write claim failed with rc %d" % rc
        contents = open(usdFile, "r").read()
        assert "capabilityUsages" in contents, \
            "expected capabilityUsages in authored file:\n%s" % contents
        assert CAPABILITY in contents, \
            "expected capability token in authored file:\n%s" % contents

        #--------------------------------------------------------------
        # Test 4. Generate another report.
        rc, out = run(["--report", "--prim", PRIM, usdFile])
        assert rc == 0, "re-report failed with rc %d" % rc
        assert CAPABILITY in out and DEGRADATION in out, \
            "expected '%s = %s' in report output:\n%s" % (
                CAPABILITY, DEGRADATION, out)

        #--------------------------------------------------------------
        # Test 5. Check that the tool won't accept two modes at once.
        rc, out = run(["--report", "--verify", "--prim", PRIM, usdFile])
        assert rc == 3, "expected rc 3 for mutually-exclusive modes, got %d" % rc

        #--------------------------------------------------------------
        # Introspection against the procedural city fixture.
        for name in CITY_FIXTURES:
            shutil.copyfile(os.path.join(SCRIPT_DIR, name),
                            os.path.join(workdir, name))
        cityFile = os.path.join(workdir, "city.usda")

        #--------------------------------------------------------------
        # Test 6. Check expected inference on /City
        before = open(cityFile, "rb").read()
        rc, out = run(["--introspect", "--prim", "/City", cityFile])
        assert rc == 0, "dry-run introspect failed with rc %d" % rc
        assert "would infer" in out, \
            "expected 'would infer' in dry-run introspect output:\n%s" % out
        assert "%s = hard" % COLORSPACE in out, \
            "expected '%s = hard' from non-instanced Ground:\n%s" % (
                COLORSPACE, out)
        # MaterialX inference is best-effort (usdMtlx plugin discovery).
        if MTLX in out:
            assert "%s = hard" % MTLX in out, \
                "if inferred, MaterialX capability should be hard:\n%s" % out
        after = open(cityFile, "rb").read()
        assert before == after, "dry-run introspect must not modify the file"

        #--------------------------------------------------------------
        # Test 7. Test writing introspection result writing.
        rc, out = run(["--introspect", "--prim", "/City", "--write", cityFile])
        assert rc == 0, "write introspect failed with rc %d" % rc
        contents = open(cityFile, "r").read()
        assert "capabilityUsages" in contents, \
            "expected capabilityUsages in authored city file:\n%s" % contents
        assert COLORSPACE in contents, \
            "expected colorspace capability in authored file:\n%s" % contents

        #--------------------------------------------------------------
        # Test 8. Confirm that reporting works after a write
        rc, out = run(["--report", "--prim", "/City", cityFile])
        assert rc == 0, "city report failed with rc %d" % rc
        assert COLORSPACE in out, \
            "expected '%s' in city report output:\n%s" % (COLORSPACE, out)

        #--------------------------------------------------------------
        # Test 9. Check that colorspace is not inferred within 
        # instanced prototypes, and that the mtlx usage appears.
        rc, out = run(["--introspect", "--prim", "/City/Block1", cityFile])
        assert rc == 0, "block introspect failed with rc %d" % rc

        # Only inspect the "would infer" section for the absence assertion; the
        # colorspace token also appears in the authored (before) dump from
        # step 7's --write, so scope the check to the inferred listing.
        inferred = out.split("would infer", 1)[-1]
        assert COLORSPACE in inferred, \
            ("usd.core.colorspace should be inferred via the instance prototype:\n%s" % out)
        if MTLX in inferred:
            assert "%s = hard" % MTLX in inferred, \
                "if inferred, MaterialX capability should be hard:\n%s" % out

        #--------------------------------------------------------------
        # Test 10. Pseudo-root defaultPrim fallback: no --prim resolves
        # to the stage's defaultPrim (City) and infers colorspace.
        rc, out = run(["--introspect", cityFile])
        assert rc == 0, "defaultPrim-fallback introspect failed with rc %d" % rc
        assert "defaultPrim" in out, \
            "expected a defaultPrim note in fallback output:\n%s" % out
        assert COLORSPACE in out, \
            "expected '%s' via defaultPrim fallback:\n%s" % (COLORSPACE, out)

        #--------------------------------------------------------------
        # Test 11. Test that json output works.
        rc, out = run(["--introspect", "--prim", "/City", "--json", cityFile])
        assert rc == 0, "--json introspect failed with rc %d" % rc
        brace = out.find("{")
        assert brace != -1, "expected a JSON object in --json output:\n%s" % out
        doc = json.loads(out[brace:])
        assert doc["mode"] == "introspect", "unexpected mode: %r" % doc
        assert doc["write"] is False, "dry-run must report write=false: %r" % doc
        assert doc["prim"] == "/City", "unexpected prim: %r" % doc
        assert doc["inferred"].get(COLORSPACE) == "hard", \
            "expected inferred colorspace=hard in JSON:\n%s" % out
        # "before" reflects state authored by step 7's --write on this file.
        assert "capabilityUsages" in doc["before"], \
            "expected before.capabilityUsages in JSON:\n%s" % out

        print("OK")
        return 0
    finally:
        shutil.rmtree(workdir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
