# D2Engine — данные для отправки в Microsoft Store

Пакет: `AppPackages\Store\Descent2_1.0.0.0_ARM_bundle.appxupload` (собирается командой из README, раздел «Store build»).

Идентификатор в манифесте (`winmobile/Package.Store.appxmanifest`): `AstrohackerML.D2Engine`, издатель `CN=731ECEE4-03DC-405A-B788-3758C0ED0488`. Перед первой отправкой зарезервируйте в Partner Center имя **D2Engine** и сверьте значения в **Product management → Product identity**: `Package/Identity/Name` и `Package/Identity/Publisher` должны совпасть с манифестом. Если Partner Center выдал другое имя пакета, поправьте `Name` в манифесте и пересоберите.

Скриншоты: нужны с телефона (1440×2560 или в альбомной ориентации 2560×1440), например главное меню, брифинг, игра с экранными кнопками. Кнопки громкость вверх + питание сохраняют снимок в «Фотографии → Снимки экрана».

---

## Свойства (Properties)

- Категория: **Игры → Боевики и приключения (Action & adventure)**
- Политика конфиденциальности: не требуется — приложение не выходит в сеть и не собирает данные
- Веб-сайт: https://github.com/AstrohackerML/Descent2-WinMobile
- Контакты поддержки: AstrohackerML@outlook.com
- Системные требования: ничего не отмечать
- Использует данные пользователя: **нет**

## Возрастной рейтинг (Age ratings)

Анкета IARC: насилие — стрельба по роботам, без людей и крови; без нецензурной лексики, азартных игр, покупок, общения с другими игроками, без доступа к интернету.

## Цены и доступность (Pricing and availability)

- Цена: **бесплатно**. Лицензия кода Parallax разрешает только некоммерческое использование: никакой цены, рекламы и встроенных покупок.
- Рынки: все
- Видимость: «Частная аудитория» для первой проверки, затем «Общедоступно в Microsoft Store»

## Пакеты (Packages)

- Загрузить `.appxupload`.
- Семейства устройств: отметить только **Windows 10 Mobile**. Пакет собран лишь для ARM32, на ПК его ставить незачем.

## Примечания для сертификации (Notes for certification)

> D2Engine is an engine port that plays the user's own copy of the 1996 PC game Descent II. It contains no game content. On first launch it asks the user to pick a folder with the Descent II data files (descent2.hog, descent2.ham, *.pig, descent2.s22) and copies them into the app's storage; without them the app shows a list of the missing files and lets the user pick another folder or quit. To test the game, use the data files of a PC copy of Descent II (for example from GOG or Steam). The app does not use the network.

---

## Описание на русском (ru-RU)

**Краткое описание**

Движок Descent II для Windows 10 Mobile: играйте в свою копию классического шутера на телефоне.

**Описание**

D2Engine — порт движка игры Descent II (1996) для телефонов с Windows 10 Mobile. Приложение не содержит игровых данных: для игры нужны файлы вашей копии Descent II для ПК (например, из GOG или Steam).

• Полная игра с сенсорным управлением.
• Экранная клавиатура для имени пилота и сохранений.
• Звук, ролики со звуком и MIDI-музыка.
• Сохранение и загрузка игры.
• При первом запуске выберите папку с файлами игры — приложение проверит, всех ли файлов хватает.

Нужные файлы: descent2.hog, descent2.ham, alien1.pig, alien2.pig, fire.pig, groupa.pig, ice.pig, water.pig, descent2.s22 или descent2.s11. Ролики (*.mvl) необязательны.

Приложение не выходит в интернет, не собирает данные и не содержит рекламы. Исходный код: github.com/AstrohackerML/Descent2-WinMobile

D2Engine — неофициальный проект, не связанный с Interplay и Parallax Software. Descent — товарный знак Interplay Entertainment.

**Возможности (до 20 строк)**

- Полная игра Descent II с вашими файлами
- Сенсорное управление
- Экранная клавиатура
- Звук, ролики и MIDI-музыка
- Сохранение и загрузка
- Проверка файлов игры при установке
- Без рекламы и сбора данных

**Ключевые слова (до 7)**

Descent, Descent II, шутер, 6DOF, ретро, Windows 10 Mobile, порт

**Что нового**

Первая версия.

**Авторские права**

© 2026 AstrohackerML. Код Descent II © 1993–1998 Parallax Software.

---

## Description in English (en-US)

**Short description**

A Descent II engine for Windows 10 Mobile: play your own copy of the classic shooter on your phone.

**Description**

D2Engine is a port of the Descent II (1996) game engine to Windows 10 Mobile phones. It contains no game data: you need the files of your own PC copy of Descent II (for example from GOG or Steam).

• The full game with touch controls.
• On-screen keyboard for pilot and save names.
• Sound, movies with sound and MIDI music.
• Saving and loading.
• On first launch, pick the folder with the game files — the app checks that none are missing.

Files needed: descent2.hog, descent2.ham, alien1.pig, alien2.pig, fire.pig, groupa.pig, ice.pig, water.pig, descent2.s22 or descent2.s11. Movies (*.mvl) are optional.

The app does not use the internet, collects no data and has no ads. Source code: github.com/AstrohackerML/Descent2-WinMobile

D2Engine is an unofficial project, not affiliated with Interplay or Parallax Software. Descent is a trademark of Interplay Entertainment.

**Product features**

- The full Descent II with your own files
- Touch controls
- On-screen keyboard
- Sound, movies and MIDI music
- Saving and loading
- Checks the game files on setup
- No ads, no data collection

**Search terms**

Descent, Descent II, shooter, 6DOF, retro, Windows 10 Mobile, port

**What's new**

First version.

**Copyright**

© 2026 AstrohackerML. Descent II code © 1993–1998 Parallax Software.
