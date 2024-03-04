:: cmd
@ECHO OFF

set BASE_URI=https://hailo-hailort.s3.eu-west-2.amazonaws.com
set HRT_VERSION=4.16.2
set FW_DIR=Hailo8/%HRT_VERSION%/FW
set FW=hailo8_fw.%HRT_VERSION%_eth.bin

:: download firmware from AWS
ECHO Downloading Hailo Firmware from S3
powershell -c "wget %BASE_URI%/%FW_DIR%/%FW% -outfile %FW%"

