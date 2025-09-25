@REM This file contains HailoRT's configurable environment variables for HailoRT Windows Service.
@REM  The environment variables are set to their default values, and are seperated by the character \0.
@REM  To change an environment variable's value, follow the steps:
@REM  1. Change the value of the selected environment variable in this file
@REM  2. Run this script
@REM  3. Restart the service
@REM Running this script requires Administrator permissions.

reg ADD HKLM\SYSTEM\CurrentControlSet\Services\hailort_service /f /v Environment /t REG_MULTI_SZ /d ^
HAILORT_LOGGER_PATH="%PROGRAMDATA%\HailoRT_Service\logs"\0^
HAILO_TRACE=0\0^
HAILO_TRACE_TIME_IN_SECONDS_BOUNDED_DUMP=0\0^
HAILO_TRACE_SIZE_IN_KB_BOUNDED_DUMP=0\0^
HAILO_TRACE_PATH=""\0
@REM TODO: HRT-7304 - Add `HAILO_MONITOR`