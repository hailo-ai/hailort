if(NOT MONTSERRAT_FONT_DIR)
    message(FATAL_ERROR "MONTSERRAT_FONT_DIR must be set before including montserrat_fonts.cmake")
endif()

# Download Montserrat font files at configure time (OFL-licensed, from Google Fonts CDN).
# Format: "<local_name>:<cdn_name>:<sha256>". Hashes pin the v31 CDN files we tested
# against, and double as integrity checks: a partial/0-byte download from a previous
# interrupted configure run will mismatch and trigger a re-download instead of being
# silently embedded as an empty array (which breaks SLINT_EMBED_RESOURCES on MSVC).
set(_montserrat_cdn "https://fonts.gstatic.com/s/montserrat/v31")
set(_montserrat_fonts
    "Montserrat-Regular.ttf:JTUHjIg1_i6t8kCHKm4532VJOt5-QNFgpCtr6Ew-.ttf:84f3d3275dfb159dd311c61828fd754a82ec31c377120518e2cd3e7d34394e96"
    "Montserrat-Medium.ttf:JTUHjIg1_i6t8kCHKm4532VJOt5-QNFgpCtZ6Ew-.ttf:637639da93fdd9178e52662cce4bd5a59a544eb60677bb6ef9f89bc472607e5d"
    "Montserrat-SemiBold.ttf:JTUHjIg1_i6t8kCHKm4532VJOt5-QNFgpCu170w-.ttf:c535bb158e8cfc3c27654f73aa68a87732899e7d9b10d8496b68605a56d947a7"
    "Montserrat-Bold.ttf:JTUHjIg1_i6t8kCHKm4532VJOt5-QNFgpCuM70w-.ttf:247f052d2f5b84a5bbe7831c3b08a71ca391ff54f1dfb9be88a3d3da569e3a38"
)

foreach(_entry ${_montserrat_fonts})
    string(REPLACE ":" ";" _parts "${_entry}")
    list(GET _parts 0 _local_name)
    list(GET _parts 1 _cdn_name)
    list(GET _parts 2 _expected_sha256)
    set(_font_file "${MONTSERRAT_FONT_DIR}/${_local_name}")

    set(_needs_download TRUE)
    if(EXISTS "${_font_file}")
        file(SHA256 "${_font_file}" _actual_sha256)
        if(_actual_sha256 STREQUAL _expected_sha256)
            set(_needs_download FALSE)
        else()
            message(STATUS "Re-downloading ${_local_name} (hash mismatch / corrupt previous download)")
            file(REMOVE "${_font_file}")
        endif()
    endif()

    if(_needs_download)
        message(STATUS "Downloading ${_local_name} ...")
        file(DOWNLOAD "${_montserrat_cdn}/${_cdn_name}" "${_font_file}"
            TIMEOUT 30
            EXPECTED_HASH SHA256=${_expected_sha256}
            STATUS _dl_status)
        list(GET _dl_status 0 _dl_code)
        if(NOT _dl_code EQUAL 0)
            list(GET _dl_status 1 _dl_msg)
            # Remove any partial file so the next configure run retries cleanly.
            file(REMOVE "${_font_file}")
            message(FATAL_ERROR "Failed to download ${_local_name}: ${_dl_msg}")
        endif()
    endif()
endforeach()
