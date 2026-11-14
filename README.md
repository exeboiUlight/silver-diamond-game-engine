# Silver Diamond Game Engine 0.4
---
## что сделано

- [x] custom window menanger
- [x] assets loaders
- [x] 3D camera
- [x] model
- [x] sun
- [x] ambient light
- [x] point light
- [x] physics engine (2D and 3D)
- [x] **scene editor** (иерархия узлов, выбор, перемещение/вращение/масштаб, наложение текстур, экспорт в glTF)
- [x] **texturing через stb_image** (PNG/JPG/BMP/TGA/HDR -> GL, sRGB для цвета)
- [x] **физика переписана**: кватернионы, тензор инерции, манифест контакта, стабильные указатели на тела

---

## сборка

```bash
python build.py            # движок + приложение
python build.py editor     # редактор сцен
python build.py run        # собрать и запустить
python build.py sdk        # выгрузить C SDK в API/
```

Подробная документация — в **[doc.md](doc.md)**.

---

## об проекте

Мы делаем этот проект как собственный движок студии Selenium groud. Делается он под нашы нужды и потребности.Будем весьма юлагодарны если вы оцените наши старания и поставится звезду или починить какую-нибудь ошибку. У нас своя система работы с объектами.
