# Driver Signing Guide — MTK USB Serial Driver

## Why Signing Is Required

Windows 10/11 requires all kernel-mode drivers (`.sys`) to be digitally signed with a trusted certificate. Without a valid signature, Windows rejects the driver with:

```
Error 0x800B0109: A certificate chain processed, but terminated in a root
certificate which is not trusted by the trust provider.
```

By default, the CI workflow signs the driver with an **ephemeral self-signed test certificate**, which Windows does not trust. To eliminate this error, you need a **production code signing certificate**.

---

## Option 1: Purchase an EV Code Signing Certificate (Recommended)

An **EV (Extended Validation) Code Signing Certificate** from a trusted Certificate Authority (CA) is the standard way to sign Windows drivers. EV certificates are trusted by Windows out of the box.

### Trusted CAs for Driver Signing

| CA | Approximate Cost | URL |
|----|-----------------|-----|
| DigiCert | ~$400-600/year | https://www.digicert.com/signing/code-signing-certificates |
| Sectigo (Comodo) | ~$200-400/year | https://sectigo.com/ssl-certificates-tls/code-signing |
| GlobalSign | ~$200-400/year | https://www.globalsign.com/en/code-signing-certificate |
| SSL.com | ~$200-400/year | https://www.ssl.com/certificates/ev-code-signing/ |

### Steps

1. **Purchase** an EV Code Signing Certificate from a trusted CA
2. **Download** the certificate as a `.pfx` (PKCS#12) file with a password
3. **Configure** the GitHub repository secrets (see [Configure GitHub Secrets](#configure-github-secrets) below)
4. **Push** to `main` — the CI will automatically sign the driver with the production certificate
5. Users can now install the driver **without test signing**

---

## Option 2: Microsoft Trusted Signing (Azure)

Microsoft offers [Azure Trusted Signing](https://learn.microsoft.com/en-us/azure/trusted-signing/) (formerly Azure Code Signing) as a cloud-based signing service, starting at ~$10/month.

### Steps

1. Create an Azure account and subscribe to **Trusted Signing**
2. Create a signing profile and certificate
3. Export the certificate as `.pfx`
4. Configure the GitHub repository secrets (see below)

---

## Option 3: Generate Your Own Certificate (Free — Requires Manual Trust)

You can generate your own code signing certificate for free. Users will need to **manually trust** the certificate on their machines, but once trusted, the driver installs without test signing.

### Step 1: Generate the Certificate

Run in **PowerShell as Administrator** on any Windows machine:

```powershell
# Generate a code signing certificate valid for 5 years
$cert = New-SelfSignedCertificate `
  -Subject "CN=MTK Loader Drivers Opensource, O=MTK Loader Drivers Opensource" `
  -Type CodeSigningCert `
  -CertStoreLocation Cert:\CurrentUser\My `
  -NotAfter (Get-Date).AddYears(5) `
  -KeyAlgorithm RSA `
  -KeyLength 4096 `
  -HashAlgorithm SHA256

Write-Host "Certificate thumbprint: $($cert.Thumbprint)"
Write-Host "Certificate subject:    $($cert.Subject)"
```

### Step 2: Export the Certificate

```powershell
# Set a strong password for the PFX
$pfxPassword = Read-Host "Enter PFX password" -AsSecureString

# Export the PFX (private key + certificate)
$pfxPath = "$env:USERPROFILE\mtk_driver_signing.pfx"
Export-PfxCertificate -Cert $cert -FilePath $pfxPath -Password $pfxPassword
Write-Host "PFX exported to: $pfxPath"

# Export the public certificate (for users to trust)
$cerPath = "$env:USERPROFILE\mtk_driver_signing.cer"
Export-Certificate -Cert $cert -FilePath $cerPath
Write-Host "Public certificate exported to: $cerPath"
```

### Step 3: Configure GitHub Secrets

See [Configure GitHub Secrets](#configure-github-secrets) below.

### Step 4: Distribute the Public Certificate

Users need to install the `.cer` file into their **Trusted Root Certification Authorities** store. Include the `.cer` file in your release and instruct users to run:

```cmd
:: Run as Administrator
certutil -addstore Root mtk_driver_signing.cer
```

Or via PowerShell:

```powershell
# Run as Administrator
Import-Certificate -FilePath "mtk_driver_signing.cer" -CertStoreLocation Cert:\LocalMachine\Root
```

After trusting the certificate, the driver installs normally — **no test signing needed**.

---

## Configure GitHub Secrets

Once you have a `.pfx` certificate file, configure these two GitHub repository secrets:

### 1. `SIGNING_CERT_BASE64`

Convert the `.pfx` file to Base64:

**PowerShell:**
```powershell
$pfxPath = "$env:USERPROFILE\mtk_driver_signing.pfx"
$base64 = [Convert]::ToBase64String([IO.File]::ReadAllBytes($pfxPath))
$base64 | Set-Clipboard
Write-Host "Base64 copied to clipboard (length: $($base64.Length) chars)"
```

**Linux/macOS:**
```bash
base64 -w 0 mtk_driver_signing.pfx | xclip -selection clipboard
```

Then go to **GitHub → Repository → Settings → Secrets and variables → Actions → New repository secret**:
- Name: `SIGNING_CERT_BASE64`
- Value: *(paste the Base64 string)*

### 2. `SIGNING_CERT_PASSWORD`

Create another secret:
- Name: `SIGNING_CERT_PASSWORD`
- Value: *(the password you set when exporting the PFX)*

### 3. Push and Verify

After configuring both secrets:

1. Push any commit to `main` (or trigger the workflow manually)
2. The CI will detect the production certificate and use it for signing
3. Check the workflow logs — you should see:
   ```
   Using production code signing certificate...
     Certificate: CN=MTK Loader Drivers Opensource, ...
     Thumbprint:  ABCDEF1234567890...
   ```
4. The release artifacts will be signed with the production certificate

---

## Summary

| Method | Cost | Trust Level | User Action Required |
|--------|------|-------------|---------------------|
| **EV Code Signing (CA)** | ~$200-600/year | Trusted by all Windows PCs | None — installs seamlessly |
| **Azure Trusted Signing** | ~$10/month | Trusted by all Windows PCs | None — installs seamlessly |
| **Self-generated + distribute .cer** | Free | Trusted after manual import | User runs `certutil -addstore Root` once |
| **Self-signed (default CI)** | Free | Not trusted | User must enable test signing |
