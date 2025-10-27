/**
 * Асинхронно загружает и вставляет HTML-контент из указанного файла.
 * @param {string} targetId ID элемента, куда вставлять.
 * @param {string} filePath Путь к HTML-файлу.
 */
async function loadComponent(targetId, filePath) {
    const targetElement = document.getElementById(targetId);
    if (!targetElement) {
        console.error(`Элемент с ID ${targetId} не найден.`);
        return;
    }
    try {
        const response = await fetch(filePath);
        if (!response.ok) {
            throw new Error(`Ошибка загрузки: ${response.statusText}`);
        }
        const html = await response.text();
        targetElement.innerHTML = html;
        
    } catch (error) {
        targetElement.innerHTML = `<p style="color:red;">Ошибка загрузки компонента: ${error.message}</p>`;
        console.error(error);
    }
}