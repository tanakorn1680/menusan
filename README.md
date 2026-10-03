# ProMenu 0.2

ปุ่มวงกลมลอย + แผงเมนูแบบสัมผัส สำหรับ GTA SA Android 2.10 (arm64, AML)

**ยังไม่เคยทดสอบในเกมจริงรอบ 0.2** โค้ดผ่านการตรวจ syntax และตรรกะการแตะบนเครื่องผมแล้ว ชื่อฟังก์ชันของเกมทุกตัวตรวจกับไบนารี ARM CheatMenu เดิม

## ใช้งาน
- แตะวงกลม = เปิด/ปิดเมนู (ปุ่ม X สีแดงก็ปิดได้) ลากวงกลมไปวางที่ไหนก็ได้ มันอยู่ตรงนั้นและจำตำแหน่งไว้
- ลากนิ้วในรายการเพื่อเลื่อนหน้า
- 6 แท็บ: Player, Vehicles, Weapons, World, Fun, Settings (รวม 142 ปุ่ม)
- Settings: ความโปร่งใส ขนาดปุ่ม เปิด/ปิดการจางเมื่อไม่ได้แตะ รีเซ็ตตำแหน่ง และตัวนับ Touches/Taps ไว้ดูตอนมีปัญหา

## คอมไพล์ (GitHub Actions)
1. อัปโหลดทุกไฟล์ในโฟลเดอร์นี้ขึ้น repo (รวม `.github/workflows/android.yml`)
2. แท็บ Actions → Build ProMenu (arm64) → Run workflow (หรือ push แล้วมันรันเอง)
3. จบแล้วดาวน์โหลด artifact `libProMenu64` ได้ไฟล์ `libProMenu64.so`
4. ถ้า build พัง บรรทัด error จะขึ้นในหน้าสรุปของ run

## ติดตั้ง
1. ย้าย `libCheatMenu64.so` เดิมออกจากโฟลเดอร์ `mods` (สองตัวแย่ง hook เดียวกัน)
2. วาง `libProMenu64.so` ใน `Android/data/com.rockstargames.gtasa/mods/`

## ไฟล์ตั้งค่าและฟอนต์
- ตำแหน่ง/ขนาดปุ่มเก็บที่ `files/ProMenu/config.txt` ลบไฟล์นี้เพื่อรีเซ็ต
- ฟอนต์: `files/ProMenu/font.ttf` ถ้ามี ไม่งั้น `files/ARM/CheatMenu/Font/MyFont.ttf` ไม่งั้นฟอนต์ในตัว

## ที่ยังไม่ได้ทำ
วาร์ป ใส่เงินตามจำนวน กระสุน/เลือดไม่จำกัด (ต้องอ่านโครงสร้างข้อมูลเกมจากไบนารีเดิมก่อน)

## Log
logcat tag `ProMenu`
