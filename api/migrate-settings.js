/**
 * migrate-settings.js
 * Скрипт для міграції налаштувань розсилки (Config) з GAS у Firestore.
 */

async function migrateSettingsToFirestore() {
    console.log("⏳ 1. Починаємо міграцію налаштувань (Config)...");

    const currentUser = firebase.auth().currentUser;
    if (!currentUser) {
       console.error("❌ Необхідно авторизуватись у системі перед міграцією!");
       alert("Помилка: Необхідно авторизуватись у системі!");
       return;
    }

    try {
        const jsonStr = await GasService.getConfig();
        const data = JSON.parse(jsonStr);

        if (!data || data.length === 0) {
            console.log("ℹ️ В GAS немає записів налаштувань.");
            alert("Помилка: GAS таблиця Config порожня.");
            return;
        }

        console.log(`✅ Отримано ${data.length} записів налаштувань. Записуємо у Firestore...`);

        let successCount = 0;
        data.forEach(async (row, index) => {
            const username = row.user_a;
            if (!username) return;

            const logData = {
                email: row.email || "",
                notifyEmail: row.flag === "1",
                notifyPush: row.flagP === "1",
                sheetRow: index + 2, // Зберігаємо оригінальний рядок GAS
                updatedAt: firebase.firestore.FieldValue.serverTimestamp()
            };

            await db.collection("notifications_config").doc(username).set(logData);
            successCount++;
        });

        const finalMsg = `🎉 Міграція налаштувань завершена! Успішно перенесено: ${successCount} профілів.`;
        console.log(finalMsg);
        alert(finalMsg);

    } catch (e) {
        console.error("❌ Помилка міграції налаштувань:", e);
        alert("Помилка при міграції: " + e.message);
    }
}

window.migrateSettingsToFirestore = migrateSettingsToFirestore;
