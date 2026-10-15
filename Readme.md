# 🎵 سورس آني بودا — النسخة الكاملة بلغة C++20 (`TDLib` + `Native C++ NTgCalls`)

تم تحويل البوت بالكامل من لغة بايثون إلى لغة **C++20** للعمل بأقصى سرعة وأقل استهلاك للرام والمعالج، مع الاعتماد المباشر على:
1. **`TDLib` (`td::ClientManager` & `td::td_api`)**: بدلاً من `Pyrogram` / `Kurigram`.
2. **`NTgCalls` C++ الأصلي (`ntgcalls::NTgCalls`)**: التعامل المباشر مع مكتبة C++ لبث الصوت والفيديو في المكالمات الجماعية **وبدون غلاف `pytgcalls` نهائياً**.
3. **الحفاظ على جميع متغيرات البيئة (`.env`) بنفس الأسماء 100%** حتى لا تحتاج لتغيير أي اسم متغير في ملف الإعدادات.

## 📋 متغيرات البيئة المحفوظة (`.env` / `sample.env`)

جميع المتغيرات التالية محفوظة بنفس أسمائها تماماً كما في النسخة الأصلية:
- `API_ID` و `API_HASH`
- `BOT_TOKEN`
- `OWNER_ID` و `OWNER_USERNAME`
- `LOGGER_ID`
- `STRING_SESSION` (بالإضافة إلى `STRING_SESSION2` حتى `STRING_SESSION5`)
- `MONGO_DB_URI`
- `COOKIE_URL`
- `DEEP_API` و `GPT_4`
- `API_KEY` و `API_URL` و `VIDEO_API_URL`
- `SUPPORT_CHANNEL` و `SUPPORT_CHAT`

> **ملاحظة مهمة حول جلسة المساعد (`STRING_SESSION`) في `TDLib`**:
> سلسلة الجلسة النصية (`STRING_SESSION`) في بايثون هي صيغة خاصة بمكتبة Pyrogram فقط ولا تقبلها مكتبة `TDLib` مباشرة لأن `TDLib` تحفظ الجلسات المشفرة في مجلد قاعدة بيانات (`td_db/assistant_1`).
> لتسهيل الأمر عليك بدون تغيير أسماء المتغيرات:
> - يمكنك وضع **رقم هاتف الحساب المساعد بالصيغة الدولية** (مثل `+2010xxxxxxxx`) داخل متغير `STRING_SESSION` في ملف `.env` (أو في `ASSISTANT_PHONE1`)، وعند أول تشغيل سيطلب منك كود التفعيل لمرة واحدة فقط (أو ضعه في `ASSISTANT_CODE1`)، وبعدها سيحفظ `TDLib` الجلسة في مجلد `td_db/` للأبد.

## 🚀 طريقة التشغيل على VPS (Ubuntu / Debian)

```bash
# 1. إعداد ملف المتغيرات .env تفاعلياً
bash setup

# 2. بناء مكتبتي TDLib و NTgCalls وترجمة البوت وتشغيله
chmod +x build.sh start
./build.sh
./start
```

## 🐳 طريقة التشغيل والرفع على الاستضافات (Docker / Fly.io / Railway / Render / Heroku)

### 1. عبر Docker & Docker Compose (الأسهل والأسرع):
```bash
docker compose up -d --build
```

### 2. على منصة Fly.io:
```bash
fly launch --copy-config
fly secrets set API_ID=... API_HASH=... BOT_TOKEN=... OWNER_ID=... LOGGER_ID=... STRING_SESSION=+201... MONGO_DB_URI=...
fly deploy
```

### 3. على Railway / Render / Koyeb / Heroku:
- ارفع الملفات على مستودع **GitHub** الخاص بك.
- اربط المستودع بالاستضافة واختر **Dockerfile** كطريقة البناء (Build Method).
- أضف نفس متغيرات `.env` في خانة **Environment Variables** في لوحة تحكم الاستضافة واضغط **Deploy**.
