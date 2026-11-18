#!/bin/bash
# For developers: writes patches/nxnative.patch again from the nxnative checkout this folder sits in: every change to nxnative's own files since the
# commit Slingshot is made for (tools/install.sh NX_COMMIT). Run it after changing a hook, and commit the patch with the change.
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
nx=$(cd "$here/../.." && pwd)
NX_COMMIT=$(sed -n 's/^NX_COMMIT=\([0-9a-f]*\).*/\1/p' "$here/tools/install.sh")
git -C "$nx" --no-optional-locks diff "$NX_COMMIT" -- . ':(exclude)dm/slingshot' > "$here/patches/nxnative.patch"
git -C "$nx" --no-optional-locks diff --stat "$NX_COMMIT" -- . ':(exclude)dm/slingshot' | tail -1
echo "wrote $here/patches/nxnative.patch"
