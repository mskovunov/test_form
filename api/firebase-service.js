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
                console.log("[FirebaseService] Лог статусу порта записано успішно.");

            } catch (error) {
                console.error("[FirebaseService] Помилка при записі лога порта:", error);
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

        findEmailByUsername: async function(username) {
            if (typeof db === 'undefined') return null;
            const searchUsername = username.toLowerCase();
            const snapshot = await db.collection("users")
              .where("usernameLower", "==", searchUsername)
              .limit(1)
              .get();
            return snapshot.empty ? null : snapshot.docs[0].data().email;
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
