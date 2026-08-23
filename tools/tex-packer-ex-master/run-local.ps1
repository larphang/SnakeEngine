$PSScriptRoot = Split-Path -Parent -Path $MyInvocation.MyCommand.Definition
if (!$PSScriptRoot) { $PSScriptRoot = Get-Location }

# Determine Node/NPM paths
$localNodePath = Join-Path $PSScriptRoot "node-portable"
$localNodeExe = Join-Path $localNodePath "node.exe"

# If node isn't in PATH and no portable node exists, download it
if (!(Get-Command node -ErrorAction SilentlyContinue) -and !(Test-Path $localNodeExe)) {
    Write-Host "Node.js no está en el PATH. Descargando Node.js portable (LTS)..." -ForegroundColor Cyan
    $zipUrl = "https://nodejs.org/dist/v20.11.1/node-v20.11.1-win-x64.zip"
    $zipFile = Join-Path $PSScriptRoot "node-temp.zip"
    
    # Download
    Invoke-WebRequest -Uri $zipUrl -OutFile $zipFile
    
    Write-Host "Extrayendo archivos..." -ForegroundColor Cyan
    # Extract
    $extractPath = Join-Path $PSScriptRoot "node-temp-extract"
    if (Test-Path $extractPath) { Remove-Item -Path $extractPath -Recurse -Force }
    Expand-Archive -Path $zipFile -DestinationPath $extractPath -Force
    
    # Move and clean up
    $extractedFolder = Get-ChildItem -Path $extractPath -Directory | Select-Object -First 1
    if (Test-Path $localNodePath) { Remove-Item -Path $localNodePath -Recurse -Force }
    Move-Item -Path $extractedFolder.FullName -Destination $localNodePath -Force
    Remove-Item -Path $extractPath -Recurse -Force
    Remove-Item -Path $zipFile -Force
    Write-Host "Node.js portable listo en $localNodePath" -ForegroundColor Green
}

# Update PATH environment variable temporarily for this session
if (Test-Path $localNodeExe) {
    Write-Host "Configurando entorno temporal para usar Node.js portable..." -ForegroundColor Cyan
    $env:PATH = "$localNodePath;$env:PATH"
}

# Verify node and npm work
Write-Host "Verificando versiones de Node y NPM:" -ForegroundColor Cyan
node -v
npm -v

# Run npm install if node_modules doesn't exist
$nodeModulesPath = Join-Path $PSScriptRoot "node_modules"
if (!(Test-Path $nodeModulesPath)) {
    Write-Host "Instalando dependencias del proyecto (npm install)..." -ForegroundColor Cyan
    npm install
}

# Start the dev server
Write-Host "Abriendo http://127.0.0.1:4000/ en el navegador..." -ForegroundColor Green
Start-Process "http://127.0.0.1:4000/"
Write-Host "Iniciando servidor de desarrollo local..." -ForegroundColor Green
npm run start
