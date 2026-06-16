#!/usr/bin/env bash
# Generate the RootBench Secure Boot signing identity. Run this ONCE.
#
# Produces under secureboot/:
#   RootBench.crt  PUBLIC certificate (PEM)  -> COMMIT to the repo (CI signs with it,
#                                               users enrol it to trust signed releases)
#   RootBench.der  PUBLIC certificate (DER)  -> COMMIT (firmware 'db' / MOK enrolment)
#   RootBench.key  PRIVATE key (PEM)         -> upload as a GitHub secret, then DELETE
#                                               (gitignored; never commit it)
#
# Then prints the exact commands to store the private key in GitHub Actions.
set -euo pipefail

OUTDIR="secureboot"
CN="${SB_CN:-RootBench Secure Boot}"
DAYS="${SB_DAYS:-3650}"
KEY="$OUTDIR/RootBench.key"
CRT="$OUTDIR/RootBench.crt"
DER="$OUTDIR/RootBench.der"
SECRET_NAME="SB_SIGNING_KEY_B64"

command -v openssl >/dev/null 2>&1 || { echo "Error: openssl not found." >&2; exit 1; }

# Portable base64-without-newlines (GNU uses -w0; BSD/macOS has no -w).
b64() { base64 -w0 "$1" 2>/dev/null || base64 "$1" | tr -d '\n'; }

mkdir -p "$OUTDIR"
if [ -e "$KEY" ] || [ -e "$CRT" ]; then
    echo "Error: $KEY or $CRT already exists — refusing to overwrite the identity." >&2
    echo "Regenerating invalidates every binary signed with the old key and forces" >&2
    echo "re-enrolment of the new cert on every machine. Delete them by hand if sure." >&2
    exit 1
fi

echo "Generating RSA-4096 Secure Boot key/cert (CN='$CN', ${DAYS} days)..."
openssl req -new -x509 -newkey rsa:4096 -nodes -batch \
    -subj "/CN=$CN/" -days "$DAYS" \
    -keyout "$KEY" -out "$CRT"
chmod 600 "$KEY"
openssl x509 -in "$CRT" -outform DER -out "$DER"

cat <<EOF

Created:
  $CRT   public cert (PEM) - COMMIT
  $DER   public cert (DER) - COMMIT (users enrol this)
  $KEY   PRIVATE key       - secret only, do NOT commit

Next steps
----------
1) Store the private key as a GitHub Actions secret (needs the 'gh' CLI, logged in):

     base64 -w0 $KEY | gh secret set $SECRET_NAME

   Or paste the base64 below into:
     GitHub -> Settings -> Secrets and variables -> Actions -> New repository secret
     Name: $SECRET_NAME

   ---------------- base64($KEY) ----------------
$(b64 "$KEY")
   ----------------------------------------------

2) Commit the PUBLIC certificate(s):

     git add $CRT $DER && git commit -m "Add Secure Boot signing certificate"

3) Delete the local private key once the secret is set (keep an OFFLINE backup
   if you ever want to re-sign without rotating the cert):

     shred -u $KEY 2>/dev/null || rm -f $KEY

The Release workflow auto-detects the $SECRET_NAME secret: present -> binaries are
signed with this key; absent (forks, self-builders) -> an unsigned build.
EOF
