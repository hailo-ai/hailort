Param(
    [Switch]$h,
    [Int]$n = 1,
    [Int]$m = 1
)

$max_processes_count = 8
$first_hef="hefs\shortcut_net_nv12.hef"
$second_hef="hefs\shortcut_net.hef"
$executable_base_name="cpp_multi_process_example"
$executable_name="$executable_base_name.exe"

# find the executable (can be under Release / Debug dirs)
$files = Get-ChildItem -Filter "$executable_name" -Recurse -File
if ($files.Count -eq 0) {
    Write-Error "No files found."
} elseif ($files.Count -gt 1) {
    Write-Host "More than one file found:"
    foreach ($file in $files) {
        Write-Host $file.FullName
    }
    Write-Error "Delete all but one of the files."
} else {
    $executable=$files.FullName
}

function Show-Help {
    Write-Host "Usage: [-h] [-n <number>] [-m <number>]"
    Write-Host "  -h    Print usage and exit"
    Write-Host "  -n    Number of processes to run example with $first_hef. Max is $max_processes_count (defualt is $max_processes_count)"
    Write-Host "  -m    Number of processes to run example with $second_hef. Max is $max_processes_count (defualt is $max_processes_count)"
}

if ($h) {
    Show-Help
    exit
}

if ($n -gt $max_processes_count) {
    Write-Host "Max processes to run each hef is $max_processes_count! Given $n for $first_hef"
    exit 1
}

if ($m -gt $max_processes_count) {
    Write-Host "Max processes to run each hef is $max_processes_count! Given $m for $second_hef"
    exit 1
}

$max_hef_count = If ($n -gt $m) { $n } Else { $m }
$i = 1

do {
    if ($i -le $n) {
        Start-Process -FilePath $executable -ArgumentList $first_hef -NoNewWindow
        Write-Host "($i / $n) starting 1st hef "
    }

    if ($i -le $m) {
        Start-Process -FilePath $executable -ArgumentList $second_hef -NoNewWindow
        Write-Host "($i / $m) starting 2nd hef "
    }

    $i++
} while ($i -le $max_hef_count)

$processes = Get-Process | Where-Object { $_.Name -in "$executable_base_name" } | ForEach-Object {
        $_ | Wait-Process
}
