$ErrorActionPreference = 'Stop'
& python "$PSScriptRoot/build-development.py" @args
exit $LASTEXITCODE
