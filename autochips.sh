#!/bin/bash

FWDN_FILE="$HOME/hellochips_project/.fwdn_path"
AUTOLINUX_DIR="$HOME/topst/"

MAINCORE_HOMEIMAGE_DIR="$HOME/topst/build/tcc8050-main/tmp/deploy/"
MOUNT_POINT="/mnt/home-dir"

MAIN_SOURCE_DIR="./src/A72-main/"
CR5_EXTERNEL_DIR="./src/R5-externel"
CR5_INTERNEL_DIR="./src/R5-internel"

CR5_DIR="$HOME/topst/cr5-bsp/sources"
CR5_IMAGE_DIR="$HOME/topst/cr5-bsp/sources/build/tcc805x/gcc/tcc805x-freertos-debug/cr5_snor.rom"

MAINCORE_HOMEIMAGE="home-directory.ext4"
CR5_IMAGE="cr5_snor.rom"

# 도움말 함수
show_help() {
    echo "사용법: $0 -c setting [경로]"
    echo "  -c setting : 빌드 경로를 설정합니다."
    echo "  -c show    : 저장된 경로를 표시합니다."
    echo "  -c main   : main source 를 home-image에 구워 자동 빌드, 이미지 생성, 복사 실행 => fwdn.bat 실행 시, home으로 구워야 함"
    echo "  -c internel : R5-internel 폴더 내 소스파일로 펌웨어 이미지 생성"
    echo "  -c externel : R5-externel 폴더 내 소스파일로 펌웨어 이미지 생성"
    exit 1
}

# fwdn 경로 설정 함수
set_fwdn_path() {
    local fwdn_path=$1
    if [ -z "$fwdn_path" ]; then
        echo "경로를 입력해야 합니다!"
        exit 1
    fi

    # 경로 유효성 확인
    if [ ! -d "$fwdn_path" ]; then
        echo "입력된 경로가 존재하지 않습니다: $fwdn_path"
        exit 1
    fi

    # 경로 저장
    echo "$fwdn_path" > "$FWDN_FILE"
    echo "빌드 경로가 설정되었습니다: $FWDN_FILE"
}

# main core
# home-directory.ext4 삭제 함수
delete_home_directory_image() {
    local image_path="${MAINCORE_HOMEIMAGE_DIR}${MAINCORE_HOMEIMAGE}"

    # 경로 확인
    if [ ! -d "$image_path" ]; then
        echo "이미 삭제되어 있습니다."
        return 1
    fi

    # 파일 존재 여부 확인 및 삭제
    if [ -f "$image_path" ]; then
        echo "삭제 중: $image_path"
        rm "$image_path"
        if [ $? -eq 0 ]; then
            echo "파일이 성공적으로 삭제되었습니다: $image_path"
        else
            echo "오류: 파일 삭제에 실패했습니다: $image_path"
            return 1
        fi
    else
        echo "파일이 존재하지 않습니다: $image_path"
    fi

    return 0
}

# home-directory.ext4 복사 함수
copy_home_image_to_fwdn() {
    local source_file="${MAINCORE_HOMEIMAGE_DIR}${MAINCORE_HOMEIMAGE}"
    local destination_path

    # FWDN_FILE 경로 확인
    if [ ! -f "$FWDN_FILE" ]; then
        echo "오류: FWDN_FILE이 존재하지 않습니다: $FWDN_FILE"
        return 1
    fi

    # FWDN_FILE에서 대상 경로 읽기
    destination_path=$(cat "$FWDN_FILE")

    # 대상 경로 유효성 확인
    if [ -z "$destination_path" ] || [ ! -d "$destination_path" ]; then
        echo "오류: FWDN_FILE에 저장된 경로가 올바르지 않거나 존재하지 않습니다: $destination_path"
        return 1
    fi

    # 소스 파일 존재 확인
    if [ ! -f "$source_file" ]; then
        echo "오류: 소스 파일이 존재하지 않습니다: $source_file"
        return 1
    fi

    # 파일 복사
    echo "Copying $source_file to $destination_path/deploy-images"
    cp "$source_file" "$destination_path/deploy-image"

    # 복사 결과 확인
    if [ $? -eq 0 ]; then
        echo "파일이 성공적으로 복사되었습니다: $destination_path/deploy-image/$(basename "$source_file")"
    else
        echo "오류: 파일 복사 실패"
        return 1
    fi

    return 0
}

run_autolinux_make_fai() {
    # 현재 디렉토리 저장
    local current_dir
    current_dir=$(pwd)

    # 대상 디렉토리로 이동
    cd "$AUTOLINUX_DIR" || { echo "오류: $AUTOLINUX_DIR 디렉토리로 이동할 수 없습니다."; return 1; }

    # autolinux 명령 실행
    echo "실행 중: ./autolinux -c make_fai"
    ./autolinux -c make_fai
    local result=$?

    # 원래 디렉토리로 복귀
    cd "$current_dir" || { echo "오류: 원래 디렉토리로 복귀할 수 없습니다."; return 1; }

    # 결과 반환
    if [ $result -eq 0 ]; then
        echo "autolinux 명령이 성공적으로 실행되었습니다."
        return 0
    else
        echo "오류: autolinux 명령 실행에 실패했습니다."
        return $result
    fi
}

# Function to mount home-directory.ext4
mount_home_directory() {
    # 경로 설정
    IMAGE_PATH="${MAINCORE_HOMEIMAGE_DIR}${MAINCORE_HOMEIMAGE}"
    MOUNT_POINT="/mnt/home-dir"

    # 마운트 포인트 생성
    if [ ! -d "$MOUNT_POINT" ]; then
        echo "Creating mount point at $MOUNT_POINT"
        sudo mkdir -p "$MOUNT_POINT"
    fi

    # 마운트
    echo "Mounting $IMAGE_PATH to $MOUNT_POINT"
    sudo mount -o loop "$IMAGE_PATH" "$MOUNT_POINT"

    # 결과 확인
    if [ $? -eq 0 ]; then
        echo "Mounted successfully. You can now access $MOUNT_POINT."
    else
        echo "Failed to mount $IMAGE_PATH."
        return 1
    fi
}

# Function to unmount home-directory.ext4
umount_home_directory() {
    # 언마운트
    echo "Unmounting $MOUNT_POINT"
    sudo umount "$MOUNT_POINT"

    # 마운트 포인트 삭제 (선택 사항)
    if [ $? -eq 0 ]; then
        echo "Unmounted successfully. Removing mount point."
        sudo rmdir "$MOUNT_POINT"
    else
        echo "Failed to unmount $MOUNT_POINT."
        return 1
    fi
}

copy_to_mounted_directory() {
    if [ ! -d "$MAIN_SOURCE_DIR" ]; then
        echo "오류: 소스 디렉토리가 존재하지 않습니다: $MAIN_SOURCE_DIR"
        return 1
    fi

    if [ ! -d "$MOUNT_POINT" ]; then
        echo "오류: 마운트 경로가 존재하지 않습니다. 먼저 마운트를 수행하세요."
        return 1
    fi

    echo "Copying subfolders from $MAIN_SOURCE_DIR to $MOUNT_POINT"
    # 하위 폴더 복사
    sudo cp -r "$MAIN_SOURCE_DIR"* "$MOUNT_POINT/"

    # 결과 확인
    if [ $? -eq 0 ]; then
        echo "모든 하위 폴더가 $MOUNT_POINT로 복사되었습니다."
    else
        echo "오류: 하위 폴더 복사 실패."
        return 1
    fi

    return 0
}

main_build() {
    run_autolinux_make_fai
    mount_home_directory
    copy_to_mounted_directory
    umount_home_directory
    copy_home_image_to_fwdn
    delete_home_directory_image
}

# mcu
# rules.mk 에 추가하는 함수
add_include_to_rules() {
    local dir_name="$1"
    local base_dir=$(basename "$dir_name")
    local include_line="include \$(MCU_BSP_APP_SAMPLE_PATH)/$base_dir/rules.mk"
    parent_dir=$(dirname "$dir_name")
    local RULES_FILE="$parent_dir/rules.mk"

    # rules.mk 파일이 없으면 생성
    if [ ! -f "$RULES_FILE" ]; then
        echo "Creating $RULES_FILE."
        touch "$RULES_FILE"
    fi

    # include 라인 추가
    if ! grep -Fxq "$include_line" "$RULES_FILE"; then
        echo "Adding include line for $dir_name to $RULES_FILE with a blank line."
        echo "" >> "$RULES_FILE"  # 줄바꿈 추가
        echo "$include_line" >> "$RULES_FILE"
    else
        echo "Include line for $dir_name already exists in $RULES_FILE."
    fi
}

# 디렉토리 탐색 및 복사 함수
copy_and_merge() {
    local src="$1"
    local dest="$2"

    # 현재 디렉토리가 없는 경우 생성
    if [ ! -d "$dest" ]; then
        mkdir -p "$dest"
        add_include_to_rules "$dest"
    fi

    # 소스 디렉토리의 파일과 서브디렉토리 순회
    for item in "$src"/*; do
        if [ -f "$item" ]; then
            # 파일인 경우 대상 디렉토리에 복사
            echo "Copying file $item to $dest"
            cp "$item" "$dest/"
        elif [ -d "$item" ]; then
            # 디렉토리인 경우 재귀적으로 복사
            sub_dir_name=$(basename "$item")
            echo "Entering directory $item"
            copy_and_merge "$item" "$dest/$sub_dir_name"
        fi
    done
}

run_autolinux_cr5() {
    # 현재 디렉토리 저장
    local current_dir
    current_dir=$(pwd)

    # 대상 디렉토리로 이동
    cd "$AUTOLINUX_DIR" || { echo "오류: $AUTOLINUX_DIR 디렉토리로 이동할 수 없습니다."; return 1; }

    # autolinux 명령 실행
    echo "실행 중: ./autolinux -c build cr5"
    ./autolinux -c build cr5
    local result=$?

    # 원래 디렉토리로 복귀
    cd "$current_dir" || { echo "오류: 원래 디렉토리로 복귀할 수 없습니다."; return 1; }

    # 결과 반환
    if [ $result -eq 0 ]; then
        echo "autolinux 명령이 성공적으로 실행되었습니다."
        return 0
    else
        echo "오류: autolinux 명령 실행에 실패했습니다."
        return $result
    fi
}

copy_image() {
    echo "Copying the boot image to the target directory..."
    local source_image="$IMAGE_SOURCE_DIR/tcc8050-main-tc-boot-5.4.159-r0.img"
    local dest_image="$IMAGE_DEST_DIR/tc-boot-tcc8050-main.img"

    if [ -f "$source_image" ]; then
        cp "$source_image" "$dest_image"
        echo "Image copied to $dest_image."
    else
        echo "Source image $source_image not found!"
        exit 1
    fi
}

# home-directory.ext4 복사 함수
copy_cr5image_to_fwdn() {
    local destination_path

    # FWDN_FILE 경로 확인
    if [ ! -f "$FWDN_FILE" ]; then
        echo "오류: FWDN_FILE이 존재하지 않습니다: $FWDN_FILE"
        return 1
    fi

    # FWDN_FILE에서 대상 경로 읽기
    destination_path=$(cat "$FWDN_FILE")

    # 대상 경로 유효성 확인
    if [ -z "$destination_path" ] || [ ! -d "$destination_path" ]; then
        echo "오류: FWDN_FILE에 저장된 경로가 올바르지 않거나 존재하지 않습니다: $destination_path"
        return 1
    fi

    # 소스 파일 존재 확인
    if [ ! -f "$CR5_IMAGE_DIR" ]; then
        echo "오류: 소스 파일이 존재하지 않습니다: $CR5_IMAGE_DIR"
        return 1
    fi

    # 파일 복사
    echo "Copying $CR5_IMAGE_DIR to $destination_path/deploy-images/"
    cp "$CR5_IMAGE_DIR" "$destination_path/deploy-images/"

    # 복사 결과 확인
    if [ $? -eq 0 ]; then
        echo "파일이 성공적으로 복사되었습니다: $destination_path/$(basename "$CR5_IMAGE_DIR")"
    else
        echo "오류: 파일 복사 실패"
        return 1
    fi

    return 0
}

internel_mcu_build() {
    copy_and_merge $CR5_INTERNEL_DIR $CR5_DIR
    run_autolinux_cr5
    copy_cr5image_to_fwdn 
}

externel_mcu_build() {
    copy_and_merge $CR5_EXTERNEL_DIR $CR5_DIR
    run_autolinux_cr5
    copy_cr5image_to_fwdn
}

# 옵션 처리
if [ $# -eq 0 ]; then
    show_help
fi

while getopts "c:" opt; do
    case $opt in
        c)
            case $OPTARG in
                setting)
                    shift 2
                    set_fwdn_path "$1"
                    ;;
                show)
                    echo "show path"
                    ;;
                main)
                    main_build
                    ;;
                externel)
                    externel_mcu_build
                    ;;
                internel)
                    internel_mcu_build
                    ;;
                *)
                    echo "잘못된 옵션입니다: $OPTARG"
                    show_help
                    ;;
            esac
            ;;
        *)
            show_help
            ;;
    esac
done
