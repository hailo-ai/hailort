:: cmd
@ECHO OFF

set BASE_URI=https://hailo-hailort.s3.eu-west-2.amazonaws.com
set FW_DIR=Hailo8/4.17.0_dev/FW
set FW=hailo8_fw.4.17.0_eth.bin

:: download firmware from AWS
ECHO Downloading Hailo Firmware from S3
powershell -c "wget %BASE_URI%/%FW_DIR%/%FW% -outfile %FW%"

