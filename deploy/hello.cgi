#!/bin/sh
# Минимальная проверка, что тариф Beget исполняет CGI.
# Залить в public_html/UTP2026/, выставить права 755 и открыть в браузере.
echo "Content-Type: text/plain; charset=utf-8"
echo ""
echo "CGI работает."
echo "Дата на сервере: $(date)"
echo "Каталог: $(pwd)"
