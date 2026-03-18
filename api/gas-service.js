// api/gas-service.js

/**
 * Сервис для инкапсуляции работы с Google Apps Script (GAS).
 * Содержит логику повторных попыток (retries) и таймаутов (Fault Tolerance).
 */
const GasService = (function() {
    
    /**
     * Обертка над fetch с поддержкой таймаута и повторных попыток.
     * @param {string} url - URL запроса
     * @param {object} options - Опции fetch (method, headers, body)
     * @param {number} retries - Количество попыток перед выбросом ошибки
     * @param {number} timeout - Таймаут в миллисекундах для одного запроса
     * @returns {Promise<Response>}
     */
    async function fetchWithRetry(url, options = {}, retries = 3, timeout = 5000) {
        for (let i = 0; i < retries; i++) {
            const controller = new AbortController();
            const id = setTimeout(() => controller.abort(), timeout);
            
            try {
                const response = await fetch(url, {
                    ...options,
                    signal: controller.signal
                });
                
                clearTimeout(id);
                
                if (!response.ok) {
                    throw new Error(`HTTP Error: ${response.status}`);
                }
                
                return response;
            } catch (err) {
                clearTimeout(id);
                console.warn(`[GAS] Спроба ${i + 1} не вдалася для ${url}: ${err.message}`);
                
                if (i === retries - 1) {
                    throw err; // Выбрасываем ошибку, если попытки исчерпаны
                }
                
                // Exponential backoff: задержка увеличивается с каждой попыткой (1с, 2с, 3с...)
                await new Promise(resolve => setTimeout(resolve, 1000 * (i + 1)));
            }
        }
    }

    return {
        /**
         * Получение истории показателей (временно возвращает HTML, на Шаге 2 будет переделано под JSON).
         * @param {string} monthParam - Строка месяца 'YYYY-MM'
         */
        getHistory: async function(monthParam = '') {
            let url = WEBAPP_URL + '?history=1';
            if (monthParam) url += `&month=${monthParam}`;
            
            // Истории может потребоваться чуть больше времени
            const response = await fetchWithRetry(url, {}, 3, 10000); 
            return response.text(); 
        },

        /**
         * Получение текущего статуса порта зарядки (busy/available).
         */
        getPortStatus: async function() {
            const response = await fetchWithRetry(WEBAPP_URL + '?getPortStatus=1', {}, 3, 5000);
            return response.text();
        },

        /**
         * Изменение статуса порта зарядки.
         * @param {string} statusValue - "1" (занят) или "0" (доступен)
         */
        setPortStatus: async function(statusValue) {
            const response = await fetchWithRetry(WEBAPP_URL, {
                method: "POST",
                headers: { "Content-Type": "application/x-www-form-urlencoded" },
                body: new URLSearchParams({ portStatus: statusValue })
            }, 3, 5000);
            return response.text();
        },

        /**
         * Получение предыдущего показателя лічильника.
         */
        getLastReading: async function() {
            const response = await fetchWithRetry(WEBAPP_URL + '?getLast=1', {}, 3, 5000);
            return response.text();
        },

        /**
         * Отправка нового показателя лічильника.
         * @param {string} name - Имя пользователя (eGolf, iONIQ и т.д.)
         * @param {string} reading - Значение показателя (например "405.5")
         */
        submitReading: async function(name, reading) {
            const data = new URLSearchParams();
            data.append("name", name);
            data.append("reading", reading.toString());

            const response = await fetchWithRetry(WEBAPP_URL, {
                method: "POST",
                headers: { "Content-Type": "application/x-www-form-urlencoded" },
                body: data.toString()
            }, 2, 8000);
            
            return response.text();
        },

        /**
         * Получение конфигурации email-рассылок (для страницы настроек).
         */
        getConfig: async function() {
            const response = await fetchWithRetry(WEBAPP_URL + '?getConfig=1', {}, 3, 5000);
            return response.text();
        },

        /**
         * Обновление конфигурации email-рассылок (добавление email или изменение флагов).
         * @param {FormData} formData - Объект FormData с параметрами
         */
        updateConfig: async function(formData) {
            const response = await fetchWithRetry(WEBAPP_URL, {
                method: "POST",
                body: formData
            }, 2, 5000);
            return response.text();
        }
    };
})();
