# Secure Boot signing

RootBench release binaries are signed so they can boot under Secure Boot once the
project certificate is enrolled. This directory holds the **public** certificate;
the **private** key lives only in a GitHub Actions secret and never in the repo.

## Files (after setup)

| File | Visibility | In git? | Purpose |
|------|-----------|---------|---------|
| `RootBench.crt` | public | **yes** | PEM cert; CI signs against it, users enrol it |
| `RootBench.der` | public | **yes** | DER cert for firmware `db` / MOK enrolment |
| `RootBench.key` | **private** | **no** (gitignored) | signing key — becomes a GitHub secret, then deleted |

## One-time setup (maintainer)

```sh
# 1. Generate the identity (RSA-4096, self-signed, 10 years).
./tools/gen-signing-key.sh

# 2. Upload the private key as a repo secret (needs the gh CLI):
base64 -w0 secureboot/RootBench.key | gh secret set SB_SIGNING_KEY_B64
#    ...or paste the base64 the script printed into
#    Settings -> Secrets and variables -> Actions -> New repository secret
#    named  SB_SIGNING_KEY_B64

# 3. Commit the public certificate(s):
git add secureboot/RootBench.crt secureboot/RootBench.der
git commit -m "Add Secure Boot signing certificate"

# 4. Delete the local private key (keep an OFFLINE backup if you want to
#    re-sign later without rotating the cert):
shred -u secureboot/RootBench.key
```

## How the build uses it

- **Release CI** (`.github/workflows/release.yml`): if the `SB_SIGNING_KEY_B64`
  secret is present, it decodes the key to `keys/ci-signing.key`, builds with
  `make iso SIGN=1 SB_KEY=keys/ci-signing.key SB_CERT=secureboot/RootBench.crt`,
  then shreds the key. If the secret is absent (forks, PRs), it builds unsigned.
- **CI build check** (`ci.yml`): always `SIGN=0` — no signing for plain build checks.
- **Self-builders**: `make` with no secret and no enrolled MOK generates a local
  self-signed pair in `keys/` and signs with that (you then enrol your own cert,
  or run `make SIGN=0`). The committed project cert here is never overwritten.

## Booting a signed release under Secure Boot

Download `RootBench.der` from the release and enrol it as a MOK:

```sh
sudo mokutil --import RootBench.der    # reboot -> MokManager -> Enroll MOK
```

or import it into the firmware `db` via your firmware setup UI.

## Rotating / losing the key

The certificate is what firmware trusts, so it must stay stable — that is why
`RootBench.crt` is committed and reused. Generating a new key means a new cert,
which **every** machine must re-enrol and which invalidates older signed binaries.
Keep an offline backup of the private key to avoid forced rotation.
