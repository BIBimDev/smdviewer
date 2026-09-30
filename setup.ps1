$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

if (-not (Get-Command node -ErrorAction SilentlyContinue)) {
    throw "Node.js was not found. Install Node.js 22 LTS or newer first."
}

Write-Host "Node:" (node --version)
Write-Host "npm :" (npm.cmd --version)
Write-Host "Installing project packages..."
npm.cmd install
Write-Host "Setup complete. Run: npm.cmd run dev"
