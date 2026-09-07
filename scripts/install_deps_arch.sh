#!/usr/bin/env bash
set -e

echo "[+] Проверка и установка зависимостей на Arch Linux..."

# В Arch Linux утилита bpftool находится в пакете 'bpf'
PKGS=(
    base-devel
    cmake
    ninja
    clang
    llvm
    libbpf
    bpf
    liburing
    linux-headers
    fmt
)

MISSING_PKGS=()
for pkg in "${PKGS[@]}"; do
    if ! pacman -Qi "$pkg" &>/dev/null; then
        MISSING_PKGS+=("$pkg")
    fi
done

if [ ${#MISSING_PKGS[@]} -eq 0 ]; then
    echo "[✓] Все необходимые пакеты уже установлены."
else
    echo "[!] Будут установлены недостающие пакеты: ${MISSING_PKGS[*]}"
    sudo pacman -S --needed --noconfirm "${MISSING_PKGS[@]}"
fi

echo "[✓] Окружение готово к сборке PHALANX."
