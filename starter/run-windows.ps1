param([string]$Qemu, [string]$Kernel, [string]$Disk)
$ErrorActionPreference = 'Stop'
if (-not $Qemu) {
    $command = Get-Command qemu-system-x86_64.exe -ErrorAction SilentlyContinue
    if ($command) { $Qemu = $command.Source }
    else {
        foreach ($candidate in @("$env:ProgramFiles\qemu\qemu-system-x86_64.exe", "${env:ProgramFiles(x86)}\qemu\qemu-system-x86_64.exe")) {
            if (Test-Path -LiteralPath $candidate) { $Qemu = $candidate; break }
        }
    }
}
if (-not $Qemu -or -not (Test-Path -LiteralPath $Qemu -PathType Leaf)) {
    throw 'Windows QEMU not found. Install from https://www.qemu.org/download/#windows or pass -Qemu with its full executable path.'
}
if (-not $Kernel) {
    $Kernel = Join-Path $PSScriptRoot 'kernel.bin'
    if (-not (Test-Path -LiteralPath $Kernel)) { $Kernel = Join-Path $PSScriptRoot 'build\kernel.bin' }
}
if (-not (Test-Path -LiteralPath $Kernel -PathType Leaf)) { throw 'kernel.bin not found. Build in Ubuntu/WSL and copy build/kernel.bin alongside this script.' }
if (-not $Disk) {
    $Disk = Join-Path $PSScriptRoot 'minios64.img'
    if (-not (Test-Path -LiteralPath $Disk)) { $Disk = Join-Path $PSScriptRoot 'disks\minios64.img' }
}
$qemuArgs = @('-machine','pc','-cpu','qemu64','-m','64','-accel','tcg','-serial','none','-monitor','none','-nic','none','-no-reboot','-no-shutdown','-kernel',$Kernel)
if (Test-Path -LiteralPath $Disk -PathType Leaf) {
    if ($Disk.Contains(',')) { throw 'Disk paths containing commas are unsupported.' }
    $qemuArgs += @('-drive', "file=$Disk,format=raw,if=ide,index=0")
} else { Write-Host 'No minios64.img found: files will remain in RAM. Copy the dedicated image from starter/disks to enable save.' }
& $Qemu @qemuArgs
if ($LASTEXITCODE -ne 0) { throw "QEMU exited with code $LASTEXITCODE" }
