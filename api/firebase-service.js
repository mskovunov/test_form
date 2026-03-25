// api/firebase-service.js

/**
 * Сервис для инкапсуляции работы с Firebase (Firestore и Auth).
 */
const FirebaseService = (function() {
    // Локальный кэш, чтобы не делать лишние запросы в БД при перемещении по UI
    let cachedUserData = null;

    return {
        /**
         * Получение профиля пользователя (включая username и роль).
         * @param {object} user - Объект пользователя из Firebase Auth
         * @param {boolean} forceRefresh - Принудительно запросить из БД, игнорируя кэш
         */
        getUserData: async function(user, forceRefresh = false) {
            if (!user) return null;
            
            // Если данные есть в кэше и не запрашивается принудительное обновление
            if (cachedUserData && !forceRefresh) {
                return cachedUserData;
            }

            if (typeof db === 'undefined') {
                console.error("[FirebaseService] Firestore не ініціалізовано.");
                return null;
            }

            try {
                const doc = await db.collection('users').doc(user.uid).get();
                if (doc.exists) {
                    cachedUserData = doc.data(); // Кэшируем результат
                    return cachedUserData;
                } else {
                    console.warn("[FirebaseService] Документ користувача не знайдено.");
                }
            } catch (error) {
                console.error("[FirebaseService] Помилка при отриманні даних користувача:", error);
            }
            return null;
        },

        /**
         * Логирование изменения статуса зарядного порта.
         * @param {object} user - Текущий авторизованный пользователь Firebase
         * @param {string} newStatus - "busy" или "available"
         * @param {string} currentUsername - Имя пользователя (например, eGolf) или email
         */
        logPortStatusChange: async function(user, newStatus, currentUsername) {
            if (!user || typeof db === 'undefined') {
                console.warn("[FirebaseService] Firestore або користувач не ініціалізовані. Логування неможливе.");
                return;
            }

            const now = new Date();
            // Форматируем время в dd.MM.yyyy HH:mm:ss без внешних библиотек
            const dateTimeString = now.toLocaleDateString('uk-UA', { 
                day: '2-digit', month: '2-digit', year: 'numeric', 
                hour: '2-digit', minute: '2-digit', second: '2-digit', 
                hour12: false 
            }).replace(',', '');

            try {
                const logData = {
                    timestamp: firebase.firestore.FieldValue.serverTimestamp(), // Время на сервере Firebase
                    localTime: dateTimeString, // Время в отформатированном виде
                    userId: user.uid,
                    username: currentUsername || user.email, 
                    status: newStatus,
                    statusUA: (newStatus === "busy" ? "Зайнятий" : "Доступний")
                };

                await db.collection('port_logs').add(logData);
                // Зберігаємо поточний стан в окремий документ для швидкого читання
                await db.collection('system_state').doc('port_status').set({ status: newStatus, timestamp: firebase.firestore.FieldValue.serverTimestamp() });
                console.log("[FirebaseService] Лог статусу порта записано успішно.");

            } catch (error) {
                console.error("[FirebaseService] Помилка при записі лога порта:", error);
            }
        },

        /**
         * Сохранение введенных пользователем показателей в Firestore
         * @param {object} user - Текущий авторизованный пользователь
         * @param {string} username - Имя машины (eGolf и т.д.)
         * @param {string} prevReading - Предыдущее значение
         * @param {string} newReading - Новое значение 
         */
        saveManualReading: async function(user, username, prevReading, newReading) {
            if (!user || typeof db === 'undefined') {
                console.warn("[FirebaseService] Firestore або користувач не ініціалізовані. Логування неможливе.");
                return false;
            }

            try {
                const prev = parseFloat(prevReading);
                const current = parseFloat(newReading);
                const diff = current - prev;

                const logData = {
                    timestamp: firebase.firestore.FieldValue.serverTimestamp(),
                    userId: user.uid,
                    username: username || user.email,
                    prevReading: isNaN(prev) ? null : prev,
                    newReading: isNaN(current) ? null : current,
                    difference: isNaN(diff) ? null : parseFloat(diff.toFixed(2))
                };

                await db.collection('manual_meters').add(logData);
                console.log("[FirebaseService] Показники успішно збережено у Firestore (manual_meters).");
                return true;
            } catch (error) {
                console.error("[FirebaseService] Помилка при записі показників у Firestore:", error);
                return false;
            }
        },

        /**
         * Получение логов портов из Firestore

         * @param {number} limitCount
         */
        getPortLogs: async function(limitCount = 200) {
            if (typeof db === 'undefined') return [];
            try {
                const snapshot = await db.collection("port_logs").orderBy("timestamp", "desc").limit(limitCount).get();
                return snapshot.empty ? [] : snapshot.docs.map(doc => doc.data());
            } catch (error) {
                console.error("[FirebaseService] Помилка завантаження логів:", error);
                throw error;
            }
        },

        getPortStatus: async function() {
            if (typeof db === 'undefined') return "available";
            try {
                const doc = await db.collection('system_state').doc('port_status').get();
                if (doc.exists) {
                    return doc.data().status || "available";
                }
            } catch (error) {
                console.error("[FirebaseService] Ошибка при чтении статуса:", error);
            }
            return "available";
        },

        getLastManualReading: async function() {
            if (typeof db === 'undefined') return null;
            try {
                const snapshot = await db.collection('manual_meters').orderBy('timestamp', 'desc').limit(1).get();
                if (!snapshot.empty) {
                    return snapshot.docs[0].data().newReading;
                }
            } catch (error) {
                console.error("[FirebaseService] Помилка завантаження ост. показника:", error);
            }
            return null;
        },

        getManualReadingsHistory: async function(monthStr) {
            if (typeof db === 'undefined') return "[]";
            try {
                let query = db.collection('manual_meters').orderBy('timestamp', 'desc');
                
                if (monthStr) {
                    const [year, month] = monthStr.split('-');
                    const startDate = new Date(year, month - 1, 1);
                    const endDate = new Date(year, month, 1);
                    query = query.where('timestamp', '>=', startDate).where('timestamp', '<', endDate);
                }

                const snapshot = await query.limit(50).get(); 
                
                const data = [];
                snapshot.forEach(doc => {
                    const row = doc.data();
                    let dateStr = "---";
                    if (row.timestamp) {
                        const date = row.timestamp.toDate();
                        const dd = String(date.getDate()).padStart(2, '0');
                        const mm = String(date.getMonth() + 1).padStart(2, '0');
                        const yyyy = date.getFullYear();
                        // Відображаємо тільки дату (без часу)
                        dateStr = `${dd}.${mm}.${yyyy}`;
                    }

                    data.push({
                        date: dateStr,
                        user: row.username || "---",
                        from: row.prevReading || 0,
                        to: row.newReading || 0,
                        diff: row.difference || 0
                    });
                });
                
                return JSON.stringify(data);
            } catch (error) {
                console.error("[FirebaseService] Помилка завантаження історії:", error);
                return "[]";
            }
        },

        findEmailByUsername: async function(username) {
            if (typeof db === 'undefined') return null;
            const searchUsername = username.toLowerCase();
            const snapshot = await db.collection("users")
              .where("usernameLower", "==", searchUsername)
              .limit(1)
              .get();
            return snapshot.empty ? null : snapshot.docs[0].data().email;
        },

        getAllConfig: async function() {
            if (typeof db === 'undefined') return "[]";
            try {
                const snapshot = await db.collection("notifications_config").get();
                const results = [];
                snapshot.forEach(doc => {
                    const data = doc.data();
                    results.push({
                        user_a: doc.id,
                        email: data.email || "",
                        flag: data.notifyEmail ? "1" : "0",
                        flagP: data.notifyPush ? "1" : "0",
                        sheetRow: data.sheetRow || null
                    });
                });
                return JSON.stringify(results);
            } catch (error) {
                console.error("[FirebaseService] Ошибка при чтении конфига:", error);
                return "[]";
            }
        },

        // Новая функция для миграции данных конфигурации
        // Предполагается, что эта функция будет вызвана отдельно для выполнения миграции
        // и не является частью обычного потока getAllConfig.
        // Если это должно быть частью getAllConfig, то логика должна быть пересмотрена.
        // В текущем виде, это отдельная асинхронная операция.
        migrateConfigData: async function(data) {
            if (typeof db === 'undefined') {
                console.warn("[FirebaseService] Firestore не ініціалізовано. Міграція неможлива.");
                return 0;
            }
            let successCount = 0;
            for (const [index, row] of data.entries()) {
                const username = row.user_a;
                if (!username) continue;

                const logData = {
                    email: row.email || "",
                    notifyEmail: row.flag === "1",
                    notifyPush: row.flagP === "1",
                    sheetRow: index + 2, // Предполагается, что данные начинаются со 2-й строки в таблице
                    updatedAt: firebase.firestore.FieldValue.serverTimestamp()
                };

                try {
                    await db.collection("notifications_config").doc(username).set(logData, { merge: true });
                    successCount++;
                } catch (error) {
                    console.error(`[FirebaseService] Ошибка при миграции конфига для ${username}:`, error);
                }
            }
            console.log(`[FirebaseService] Міграція завершена. Успішно оновлено ${successCount} записів.`);
            return successCount;
        },

        updateConfigEmail: async function(username, newEmail) {
            if (typeof db === 'undefined' || !username) return false;
            try {
                await db.collection("notifications_config").doc(username).set(
                    { email: newEmail, updatedAt: firebase.firestore.FieldValue.serverTimestamp() }, 
                    { merge: true }
                );
                return true;
            } catch (error) {
                console.error("[FirebaseService] Ошибка записи email:", error);
                return false;
            }
        },

        updateConfigFlags: async function(username, flagType, flagValue) {
            if (typeof db === 'undefined' || !username) return false;
            try {
                const updateObj = { updatedAt: firebase.firestore.FieldValue.serverTimestamp() };
                updateObj[flagType] = flagValue;
                await db.collection("notifications_config").doc(username).set(updateObj, { merge: true });
                return true;
            } catch (error) {
                console.error("[FirebaseService] Ошибка записи флага:", error);
                return false;
            }
        },

        checkUsernameUnique: async function(username) {
            if (typeof db === 'undefined') return false;
            const searchUsername = username.toLowerCase();
            const snapshot = await db.collection("users")
              .where("usernameLower", "==", searchUsername)
              .limit(1)
              .get();
            return snapshot.empty;
        },

        saveUserAuthData: async function(user, username = null) {
            if (!user || typeof db === 'undefined') return;
            const userRef = db.collection("users").doc(user.uid);
            const updateData = {
              email: user.email,
              lastSignInTime: firebase.firestore.FieldValue.serverTimestamp(),
            };
            if (username) {
              updateData.username = username;
              updateData.usernameLower = username.toLowerCase();
            }
            try {
              if (user.metadata && user.metadata.creationTime === user.metadata.lastSignInTime) {
                updateData.createdAt = firebase.firestore.FieldValue.serverTimestamp();
              }
            } catch (e) {
              updateData.createdAt = firebase.firestore.FieldValue.serverTimestamp();
            }
            try {
                await userRef.set(updateData, { merge: true });
            } catch (error) {
                console.error("[FirebaseService] Ошибка при записи данных пользователя:", error);
            }
        },

        /**
         * Выход пользователя из системы (Sign Out)
         */
        signOut: async function() {
            if (typeof auth === 'undefined') return;
            try {
                await auth.signOut();
                this.clearCache();
                console.log("[FirebaseService] Успішний вихід.");
            } catch (error) {
                console.error("[FirebaseService] Помилка виходу:", error);
                throw error;
            }
        },

        /**
         * Очистка внутреннего кэша (например, при выходе из системы)
         */
        clearCache: function() {
            cachedUserData = null;
        }
    };
})();
