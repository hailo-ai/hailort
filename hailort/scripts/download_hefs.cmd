:: cmd
@ECHO OFF
set BASE_URI=https://hailo-hailort.s3.eu-west-2.amazonaws.com
set HRT_VERSION=5.1.1
set REMOTE_HEF_DIR=Hailo10/%HRT_VERSION%/HEFS
set LOCAL_EXAMPLES_HEF_DIR=..\libhailort\examples\hefs
set LOCAL_TUTORIALS_HEF_DIR=..\libhailort\bindings\python\platform\hailo_tutorials\hefs
set EXAMPLES_HEFS=(multi_network_shortcut_net.hef shortcut_net.hef shortcut_net_nv12.hef)
set TUTORIALS_HEFS=(resnet_v1_18.hef shortcut_net.hef)

if not exist %LOCAL_EXAMPLES_HEF_DIR% mkdir %LOCAL_EXAMPLES_HEF_DIR%
if not exist %LOCAL_TUTORIALS_HEF_DIR% mkdir %LOCAL_TUTORIALS_HEF_DIR%

ECHO Downloading HEFs from S3
(for %%h in %EXAMPLES_HEFS% do (
	powershell -c "wget %BASE_URI%/%REMOTE_HEF_DIR%/%%h -outfile %LOCAL_EXAMPLES_HEF_DIR%\%%h"
))
(for %%h in %TUTORIALS_HEFS% do (
	powershell -c "wget %BASE_URI%/%REMOTE_HEF_DIR%/%%h -outfile %LOCAL_TUTORIALS_HEF_DIR%\%%h"
))
