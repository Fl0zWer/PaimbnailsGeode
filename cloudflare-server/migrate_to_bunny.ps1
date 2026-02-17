# Script to migrate from Cloudflare R2 to Bunny.net using Rclone
# Auto-downloads Rclone if not found.

$ErrorActionPreference = "Stop"

# Configuration
$BunnyZone = "paimbnails"
$BunnyPassword = "074b91c9-6631-4670-a6f08a2ce970-0183-471b"
$BunnyEndpoint = "https://s3.storage.bunnycdn.com" # Default Bunny S3 endpoint

# R2 Configuration (From wrangler.toml)
$R2Endpoint = "https://0ae6862409679847f3a0d2d4171928c6.r2.cloudflarestorage.com"
$R2Buckets = @("image-converter", "sistemthumbnails")

# Working Directory
$WorkDir = $PSScriptRoot
$RcloneDir = Join-Path $WorkDir "rclone_bin"
$RcloneExe = Join-Path $RcloneDir "rclone.exe"
$RcloneConf = Join-Path $WorkDir "rclone.conf"

# 1. Download Rclone if missing
if (-not (Test-Path $RcloneExe)) {
    Write-Host "Downloading Rclone..." -ForegroundColor Cyan
    $ZipPath = Join-Path $WorkDir "rclone.zip"
    Invoke-WebRequest -Uri "https://downloads.rclone.org/rclone-current-windows-amd64.zip" -OutFile $ZipPath
    
    Write-Host "Extracting Rclone..." -ForegroundColor Cyan
    Expand-Archive -Path $ZipPath -DestinationPath $WorkDir -Force
    
    # Move executable to simpler path
    $ExtractedFolder = Get-ChildItem $WorkDir -Directory | Where-Object { $_.Name -like "rclone-*-windows-amd64" } | Select-Object -First 1
    New-Item -ItemType Directory -Path $RcloneDir -Force | Out-Null
    Move-Item (Join-Path $ExtractedFolder.FullName "rclone.exe") $RcloneExe -Force
    
    # Cleanup
    Remove-Item $ZipPath -Force
    Remove-Item $ExtractedFolder.FullName -Recurse -Force
    Write-Host "Rclone installed to $RcloneExe" -ForegroundColor Green
}

# 2. Get R2 Credentials
Write-Host "`n--- Cloudflare R2 Credentials Required ---" -ForegroundColor Yellow
Write-Host "You can find these in Cloudflare Dashboard > R2 > Manage R2 API Tokens"
$R2AccessKey = Read-Host "Enter R2 Access Key ID"
$R2SecretKey = Read-Host "Enter R2 Secret Access Key"

if ([string]::IsNullOrWhiteSpace($R2AccessKey) -or [string]::IsNullOrWhiteSpace($R2SecretKey)) {
    Write-Error "R2 Credentials are required."
}

# 3. Create Rclone Config
Write-Host "`nCreating configuration..." -ForegroundColor Cyan
$ConfigContent = @"
[r2]
type = s3
provider = Cloudflare
access_key_id = $R2AccessKey
secret_access_key = $R2SecretKey
endpoint = $R2Endpoint
acl = private

[bunny]
type = s3
provider = Other
access_key_id = $BunnyZone
secret_access_key = $BunnyPassword
endpoint = $BunnyEndpoint
acl = private
"@

Set-Content -Path $RcloneConf -Value $ConfigContent

# 4. Perform Migration
Write-Host "`nStarting Migration..." -ForegroundColor Green

foreach ($Bucket in $R2Buckets) {
    Write-Host "`nMigrating bucket: $Bucket" -ForegroundColor Cyan
    
    # Map R2 bucket names to Bunny folders
    # image-converter -> thumbnails
    # sistemthumbnails -> system
    
    $TargetFolder = $Bucket
    if ($Bucket -eq "image-converter") { $TargetFolder = "thumbnails" }
    if ($Bucket -eq "sistemthumbnails") { $TargetFolder = "system" }
    
    $Command = "& `"$RcloneExe`" copy r2:$Bucket bunny:$BunnyZone/$TargetFolder --config `"$RcloneConf`" --progress --transfers 16"
    Write-Host "Executing: $Command"
    Invoke-Expression $Command
}

Write-Host "`nMigration Completed!" -ForegroundColor Green
Write-Host "You can delete the temporary rclone files in $WorkDir if you wish."
