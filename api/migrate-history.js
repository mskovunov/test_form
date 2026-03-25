// api/migrate-history.js

/**
 * Скрипт для міграції історичних показників лічильників з Google Apps Script (GAS) в Firestore.
 * Використовується адміністраторами для перенесення всіх накопичених даних.
 */
async function migrateAllHistoryToFirestore() {
    console.log("⏳ 1. Починаємо процес повної міграції всіх місяців...");
    
    // Перевірка авторизації
    const currentUser = firebase.auth().currentUser;
    if (!currentUser) {
       console.error("❌ Необхідно авторизуватись у системі!");
       alert("Помилка: Необхідно авторизуватись у системі!");
       return;
    }

    // 1. Очистка
    console.log("🧹 2. Очищення існуючих даних у Firestore...");
    try {
        const snapshot = await db.collection("manual_meters").get();
        let deletedCount = 0;
        const batch = db.batch();
        snapshot.docs.forEach((doc) => {
            batch.delete(doc.ref);
            deletedCount++;
        });
        
        if (deletedCount > 0) {
            await batch.commit();
            console.log(`✅ Видалено ${deletedCount} старих записів.`);
        } else {
            console.log("ℹ️ Колекція вже порожня.");
        }
    } catch (e) {
        console.error("❌ Помилка при очищенні бази:", e);
        alert("Помилка при очищенні бази: " + e.message);
        return;
    }

    // 2. Генерація масиву місяців (від Лютого 2025 до поточного)
    function getMonthList() {
        const startDate = new Date(2025, 1, 1); 
        const currentDate = new Date();
        currentDate.setDate(1); 
        const result = [];
        while (currentDate >= startDate) {
            const year = currentDate.getFullYear();
            const month = currentDate.getMonth();
            const val = `${year}-${(month + 1).toString().padStart(2, '0')}`;
            result.push(val);
            currentDate.setMonth(currentDate.getMonth() - 1);
        }
        return result;
    }

    const months = getMonthList();
    console.log(`📅 Знайдено ${months.length} місяців для завантаження:`, months);

    let totalSuccessCount = 0;
    let totalRecordsFound = 0;

    // 3. Цикл по кожному місяцю
    for (const monthStr of months) {
        console.log(`⏳ Завантаження історії за місяць: ${monthStr}...`);
        try {
            const jsonStr = await GasService.getHistory(monthStr); 
            const data = JSON.parse(jsonStr);
            
            if (!data || data.length === 0) {
                console.log(`⏭️ За ${monthStr} немає записів. Пропускаємо.`);
                continue;
            }
            
            totalRecordsFound += data.length;
            console.log(`🔄 Знайдено ${data.length} записів за ${monthStr}. Зберігаємо у Firestore...`);

            // Зберігаємо записи цього місяця
            for (const row of data) {
                let timestamp = null;

                if (row.date) {
                    const dateStr = String(row.date).trim();
                    // Регулярний вираз для пошуку DD.MM.YYYY та необов'язкового часу HH:mm:ss
                    const regex = /(\d{1,2})\.(\d{1,2})\.(\d{2,4})(?:[^\d]+(\d{1,2}):(\d{1,2})(?::(\d{1,2}))?)?/;
                    const matches = dateStr.match(regex);
                    
                    if (matches) {
                        const day = parseInt(matches[1], 10);
                        const month = parseInt(matches[2], 10) - 1; // 0-11
                        let year = parseInt(matches[3], 10);
                        if (year < 100) year += 2000;
                        
                        const hours = matches[4] ? parseInt(matches[4], 10) : 0;
                        const mins = matches[5] ? parseInt(matches[5], 10) : 0;
                        const secs = matches[6] ? parseInt(matches[6], 10) : 0;
                        
                        const d = new Date(year, month, day, hours, mins, secs);
                        if (!isNaN(d.getTime())) {
                            timestamp = firebase.firestore.Timestamp.fromDate(d);
                        }
                    } else {
                        // Фолбек для інших форматів (напр. ISO або стандартні англійські дати)
                        const fallbackDate = new Date(dateStr);
                        if (!isNaN(fallbackDate.getTime())) {
                            timestamp = firebase.firestore.Timestamp.fromDate(fallbackDate);
                        }
                    }
                }

                if (!timestamp) {
                    console.warn(`⚠️ Не вдалося розпізнати дату для: "${row.date}". Встановлено поточний час.`);
                    timestamp = firebase.firestore.FieldValue.serverTimestamp();
                }

                const prev = parseFloat(row.from);
                const current = parseFloat(row.to);
                const diff = parseFloat(row.diff);

                const logData = {
                    timestamp: timestamp,
                    userId: currentUser.uid, 
                    username: row.user || "Unknown",
                    prevReading: isNaN(prev) ? null : prev,
                    newReading: isNaN(current) ? null : current,
                    difference: isNaN(diff) ? null : diff
                };

                await db.collection("manual_meters").add(logData);
                totalSuccessCount++;
            }
        } catch (e) {
            console.error(`❌ Помилка при обробці місяця ${monthStr}:`, e);
        }
    }
    
    const finalMsg = `🎉 Міграція завершена! Успішно перенесено: ${totalSuccessCount} з ${totalRecordsFound} знайдених записів.`;
    console.log(finalMsg);
    alert(finalMsg);
}

// Робимо функцію глобальною для виклику з кнопок
window.migrateAllHistoryToFirestore = migrateAllHistoryToFirestore;
