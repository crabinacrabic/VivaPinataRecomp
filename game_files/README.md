# game_files/

Извлечённый образ диска (extract-xiso 2.7.1 из Redump ISO, 1009 файлов, 5,2 ГБ).
Содержимое в .gitignore, кроме этого файла.

    default.xex        entrypoint (манифест: [entrypoint].file_path)
    Beta/              корень данных Rare; guest-путь game:\Beta\... (game_data_root = эта папка)
    $SystemUpdate/     системное обновление с диска, не используется
    gardensave.png, settingssave.png   иконки сохранений

Заново извлечь:
    extract-xiso.exe -x -d game_files "Viva Pinata (USA, Europe) (...).iso"
