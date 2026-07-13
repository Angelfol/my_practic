-- ============================================================================
-- Информационная система аптеки — схема базы данных
-- MySQL / MariaDB, InnoDB, utf8mb4
--
-- Схема построена по «Примерной структуре таблиц» из задания и приведена
-- к третьей нормальной форме (3НФ). Каждое отклонение от примерной
-- структуры помечено комментарием с обоснованием.
-- ============================================================================

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

DROP TABLE IF EXISTS uploads;
DROP TABLE IF EXISTS sessions;
DROP TABLE IF EXISTS users;
DROP TABLE IF EXISTS supplier_order_items;
DROP TABLE IF EXISTS supplier_orders;
DROP TABLE IF EXISTS order_ingredients;
DROP TABLE IF EXISTS orders;
DROP TABLE IF EXISTS prescriptions;
DROP TABLE IF EXISTS doctors;
DROP TABLE IF EXISTS customers;
DROP TABLE IF EXISTS inventory;
DROP TABLE IF EXISTS technology_ingredients;
DROP TABLE IF EXISTS technologies;
DROP TABLE IF EXISTS ingredients;
DROP TABLE IF EXISTS medicines;
DROP TABLE IF EXISTS suppliers;

SET FOREIGN_KEY_CHECKS = 1;

-- ----------------------------------------------------------------------------
-- 1. Поставщики
-- Отклонение от примерной структуры: атрибут expected_delivery переименован
-- в delivery_days (типовой срок поставки в днях). Конкретная ожидаемая дата
-- поставки — это свойство заявки (supplier_orders.expected_date), а не
-- поставщика; хранить её в suppliers означало бы зависимость атрибута
-- не от ключа (нарушение 3НФ).
-- ----------------------------------------------------------------------------
CREATE TABLE suppliers (
    supplier_id     INT UNSIGNED NOT NULL AUTO_INCREMENT,
    supplier_name   VARCHAR(150) NOT NULL,
    contact_person  VARCHAR(150) NOT NULL,
    phone           VARCHAR(20)  NOT NULL,
    email           VARCHAR(150) NOT NULL,
    address         VARCHAR(255) NOT NULL,
    delivery_days   INT UNSIGNED NOT NULL DEFAULT 3, -- типовой срок поставки, дней
    PRIMARY KEY (supplier_id),
    KEY idx_suppliers_name (supplier_name)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ----------------------------------------------------------------------------
-- 2. Лекарства (готовые и изготавливаемые)
-- Добавлены поля description и image_path: модуль «Информация о
-- товарах/услугах» обязан выводить наименование, описание и изображение
-- из БД, а по функциональным требованиям пути к файлам хранятся в БД.
-- ----------------------------------------------------------------------------
CREATE TABLE medicines (
    medicine_id           INT UNSIGNED NOT NULL AUTO_INCREMENT,
    medicine_name         VARCHAR(150) NOT NULL,
    medicine_type         ENUM('готовое','изготавливаемое') NOT NULL,
    category              VARCHAR(100) NOT NULL,           -- таблетки, мазь, микстура, раствор, порошок, настойка
    manufacturer          VARCHAR(150) NULL,               -- NULL для изготавливаемых аптекой
    form                  VARCHAR(100) NOT NULL,           -- лекарственная форма
    dosage                VARCHAR(50)  NOT NULL,
    unit                  VARCHAR(20)  NOT NULL,
    selling_price         DECIMAL(10,2) NOT NULL,
    critical_level        INT UNSIGNED NOT NULL DEFAULT 10, -- критическая норма на складе
    preferred_supplier_id INT UNSIGNED NULL,
    description           TEXT NOT NULL,                   -- добавлено: описание для каталога товаров
    image_path            VARCHAR(255) NULL,               -- добавлено: путь к изображению (хранится в БД)
    PRIMARY KEY (medicine_id),
    KEY idx_medicines_name (medicine_name),                -- индекс под поиск по наименованию
    CONSTRAINT fk_medicines_supplier FOREIGN KEY (preferred_supplier_id)
        REFERENCES suppliers (supplier_id) ON DELETE SET NULL ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ----------------------------------------------------------------------------
-- 3. Ингредиенты (вещества для изготовления лекарств)
-- Отклонение от примерной структуры: текстовый атрибут supplier_name заменён
-- внешним ключом supplier_id. Название поставщика зависит от поставщика,
-- а не от ингредиента (транзитивная зависимость — нарушение 3НФ), и его
-- дублирование привело бы к аномалиям обновления.
-- ----------------------------------------------------------------------------
CREATE TABLE ingredients (
    ingredient_id   INT UNSIGNED NOT NULL AUTO_INCREMENT,
    ingredient_name VARCHAR(150) NOT NULL,
    ingredient_type VARCHAR(100) NOT NULL,                 -- субстанция, растворитель, основа и т.п.
    supplier_id     INT UNSIGNED NULL,                     -- вместо supplier_name (3НФ)
    purchase_price  DECIMAL(10,2) NOT NULL,
    unit            VARCHAR(20)  NOT NULL,
    critical_level  INT UNSIGNED NOT NULL DEFAULT 100,     -- критическая норма на складе
    PRIMARY KEY (ingredient_id),
    KEY idx_ingredients_name (ingredient_name),
    CONSTRAINT fk_ingredients_supplier FOREIGN KEY (supplier_id)
        REFERENCES suppliers (supplier_id) ON DELETE SET NULL ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ----------------------------------------------------------------------------
-- 4. Технологии приготовления лекарств
-- Отклонение от примерной структуры: удалены атрибуты medicine_name и
-- medicine_type — они зависят от medicine_id (транзитивная зависимость,
-- нарушение 3НФ) и уже хранятся в таблице medicines; наименование
-- получается соединением по внешнему ключу.
-- ----------------------------------------------------------------------------
CREATE TABLE technologies (
    technology_id            INT UNSIGNED NOT NULL AUTO_INCREMENT,
    medicine_id              INT UNSIGNED NOT NULL,
    preparation_method       TEXT NOT NULL,                -- способ приготовления
    preparation_time_minutes INT UNSIGNED NOT NULL,
    temperature_conditions   VARCHAR(100) NULL,
    equipment_needed         VARCHAR(255) NULL,
    storage_conditions       VARCHAR(255) NULL,
    PRIMARY KEY (technology_id),
    CONSTRAINT fk_technologies_medicine FOREIGN KEY (medicine_id)
        REFERENCES medicines (medicine_id) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ----------------------------------------------------------------------------
-- 5. Ингредиенты технологий (состав лекарства по технологии)
-- ----------------------------------------------------------------------------
CREATE TABLE technology_ingredients (
    tech_ingredient_id INT UNSIGNED NOT NULL AUTO_INCREMENT,
    technology_id      INT UNSIGNED NOT NULL,
    ingredient_id      INT UNSIGNED NOT NULL,
    quantity           DECIMAL(10,3) NOT NULL,
    unit               VARCHAR(20) NOT NULL,
    PRIMARY KEY (tech_ingredient_id),
    UNIQUE KEY uq_tech_ingredient (technology_id, ingredient_id),
    CONSTRAINT fk_ti_technology FOREIGN KEY (technology_id)
        REFERENCES technologies (technology_id) ON DELETE CASCADE ON UPDATE CASCADE,
    CONSTRAINT fk_ti_ingredient FOREIGN KEY (ingredient_id)
        REFERENCES ingredients (ingredient_id) ON DELETE RESTRICT ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ----------------------------------------------------------------------------
-- 6. Складской учёт (инвентаризация)
-- Партия хранит либо готовое лекарство (medicine_id), либо ингредиент
-- (ingredient_id) — ровно одно из двух, что контролируется CHECK-ограничением.
-- ----------------------------------------------------------------------------
CREATE TABLE inventory (
    inventory_id     INT UNSIGNED NOT NULL AUTO_INCREMENT,
    medicine_id      INT UNSIGNED NULL,
    ingredient_id    INT UNSIGNED NULL,
    quantity         DECIMAL(12,3) NOT NULL,
    storage_location VARCHAR(100) NOT NULL,
    received_date    DATE NOT NULL,
    expiry_date      DATE NOT NULL,
    batch_number     VARCHAR(50) NOT NULL,
    PRIMARY KEY (inventory_id),
    KEY idx_inventory_expiry (expiry_date),
    -- ON UPDATE RESTRICT: MariaDB не допускает CHECK-ограничение на колонках
    -- внешнего ключа с каскадным изменением; ключи автоинкрементные и не меняются
    CONSTRAINT fk_inventory_medicine FOREIGN KEY (medicine_id)
        REFERENCES medicines (medicine_id) ON DELETE CASCADE ON UPDATE RESTRICT,
    CONSTRAINT fk_inventory_ingredient FOREIGN KEY (ingredient_id)
        REFERENCES ingredients (ingredient_id) ON DELETE CASCADE ON UPDATE RESTRICT,
    CONSTRAINT chk_inventory_target CHECK (
        (medicine_id IS NULL AND ingredient_id IS NOT NULL) OR
        (medicine_id IS NOT NULL AND ingredient_id IS NULL)
    )
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ----------------------------------------------------------------------------
-- 7. Покупатели
-- ----------------------------------------------------------------------------
CREATE TABLE customers (
    customer_id INT UNSIGNED NOT NULL AUTO_INCREMENT,
    last_name   VARCHAR(100) NOT NULL,
    first_name  VARCHAR(100) NOT NULL,
    middle_name VARCHAR(100) NULL,
    birth_date  DATE NOT NULL,
    phone       VARCHAR(20) NOT NULL,
    address     VARCHAR(255) NOT NULL,
    email       VARCHAR(150) NULL,
    PRIMARY KEY (customer_id),
    KEY idx_customers_name (last_name, first_name)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ----------------------------------------------------------------------------
-- 8. Врачи
-- ----------------------------------------------------------------------------
CREATE TABLE doctors (
    doctor_id      INT UNSIGNED NOT NULL AUTO_INCREMENT,
    last_name      VARCHAR(100) NOT NULL,
    first_name     VARCHAR(100) NOT NULL,
    middle_name    VARCHAR(100) NULL,
    specialization VARCHAR(100) NOT NULL,
    license_number VARCHAR(50) NOT NULL,
    phone          VARCHAR(20) NOT NULL,
    PRIMARY KEY (doctor_id),
    UNIQUE KEY uq_doctors_license (license_number)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ----------------------------------------------------------------------------
-- 9. Рецепты
-- Отклонение от примерной структуры: текстовый атрибут medicine_name заменён
-- внешним ключом medicine_id. Наименование лекарства зависит от лекарства,
-- а не от рецепта (транзитивная зависимость — нарушение 3НФ); дублирование
-- строки привело бы к аномалиям обновления.
-- ----------------------------------------------------------------------------
CREATE TABLE prescriptions (
    prescription_id    INT UNSIGNED NOT NULL AUTO_INCREMENT,
    doctor_id          INT UNSIGNED NOT NULL,
    customer_id        INT UNSIGNED NOT NULL,
    prescription_date  DATE NOT NULL,
    diagnosis          VARCHAR(255) NOT NULL,
    medicine_id        INT UNSIGNED NOT NULL,              -- вместо medicine_name (3НФ)
    quantity           INT UNSIGNED NOT NULL,
    dosage             VARCHAR(100) NOT NULL,
    usage_instructions VARCHAR(255) NOT NULL,              -- способ применения
    is_valid           TINYINT(1) NOT NULL DEFAULT 1,
    PRIMARY KEY (prescription_id),
    CONSTRAINT fk_prescriptions_doctor FOREIGN KEY (doctor_id)
        REFERENCES doctors (doctor_id) ON DELETE RESTRICT ON UPDATE CASCADE,
    CONSTRAINT fk_prescriptions_customer FOREIGN KEY (customer_id)
        REFERENCES customers (customer_id) ON DELETE RESTRICT ON UPDATE CASCADE,
    CONSTRAINT fk_prescriptions_medicine FOREIGN KEY (medicine_id)
        REFERENCES medicines (medicine_id) ON DELETE RESTRICT ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ----------------------------------------------------------------------------
-- 10. Заказы (справочник заказов: в ожидании компонентов, в производстве,
--     готовые, выданные)
-- Отклонения от примерной структуры:
--   * удалён атрибут medicine_name — дублировал medicines.medicine_name
--     (транзитивная зависимость от medicine_id, нарушение 3НФ);
--   * удалён атрибут supplier_id — связь с поставщиком существует только
--     через заявки на оптовые склады (supplier_orders), у самого заказа
--     покупателя поставщика нет (атрибут не зависел от ключа заказа);
--   * customer_id допускает NULL: заказ, оформленный сотрудником через
--     веб-сервис, может не иметь привязки к покупателю из справочника;
--   * created_at имеет тип DATE — по заданию поле «Дата» заказа вводится
--     и редактируется пользователем в форме.
-- ----------------------------------------------------------------------------
CREATE TABLE orders (
    order_id        INT UNSIGNED NOT NULL AUTO_INCREMENT,
    order_number    VARCHAR(30) NOT NULL,                  -- идентификационный номер заказа
    customer_id     INT UNSIGNED NULL,
    prescription_id INT UNSIGNED NULL,                     -- NULL для готовых лекарств без рецепта
    medicine_id     INT UNSIGNED NOT NULL,
    quantity        INT UNSIGNED NOT NULL,
    created_at      DATE NOT NULL,
    ready_time      DATETIME NULL,
    pickup_time     DATETIME NULL,
    status          ENUM('новый','ожидает компоненты','в производстве','готов','выдан','отменён')
                    NOT NULL DEFAULT 'новый',
    total_price     DECIMAL(10,2) NOT NULL,
    PRIMARY KEY (order_id),
    UNIQUE KEY uq_orders_number (order_number),            -- индекс под поиск по номеру заказа
    KEY idx_orders_created (created_at),
    CONSTRAINT fk_orders_customer FOREIGN KEY (customer_id)
        REFERENCES customers (customer_id) ON DELETE SET NULL ON UPDATE CASCADE,
    CONSTRAINT fk_orders_prescription FOREIGN KEY (prescription_id)
        REFERENCES prescriptions (prescription_id) ON DELETE SET NULL ON UPDATE CASCADE,
    CONSTRAINT fk_orders_medicine FOREIGN KEY (medicine_id)
        REFERENCES medicines (medicine_id) ON DELETE RESTRICT ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ----------------------------------------------------------------------------
-- 11. Ингредиенты заказа (резервирование компонентов под заказ)
-- Отклонения от примерной структуры:
--   * удалён атрибут ingredient_name — дублировал ingredients.ingredient_name
--     (транзитивная зависимость от ingredient_id, нарушение 3НФ);
--   * удалён флаг available — это вычислимое значение
--     (available_quantity >= required_quantity); хранение вычислимых
--     атрибутов нарушает 3НФ и ведёт к рассогласованию данных.
-- ----------------------------------------------------------------------------
CREATE TABLE order_ingredients (
    order_ingredient_id INT UNSIGNED NOT NULL AUTO_INCREMENT,
    order_id            INT UNSIGNED NOT NULL,
    ingredient_id       INT UNSIGNED NOT NULL,
    required_quantity   DECIMAL(10,3) NOT NULL,
    available_quantity  DECIMAL(10,3) NOT NULL DEFAULT 0,
    used_quantity       DECIMAL(10,3) NOT NULL DEFAULT 0,
    PRIMARY KEY (order_ingredient_id),
    UNIQUE KEY uq_order_ingredient (order_id, ingredient_id),
    CONSTRAINT fk_oi_order FOREIGN KEY (order_id)
        REFERENCES orders (order_id) ON DELETE CASCADE ON UPDATE CASCADE,
    CONSTRAINT fk_oi_ingredient FOREIGN KEY (ingredient_id)
        REFERENCES ingredients (ingredient_id) ON DELETE RESTRICT ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ----------------------------------------------------------------------------
-- 12. Заявки на оптовые склады (поставщикам)
-- ----------------------------------------------------------------------------
CREATE TABLE supplier_orders (
    supplier_order_id INT UNSIGNED NOT NULL AUTO_INCREMENT,
    supplier_id       INT UNSIGNED NOT NULL,
    order_date        DATE NOT NULL,
    expected_date     DATE NOT NULL,
    received_date     DATE NULL,
    status            ENUM('отправлена','в пути','получена','отменена')
                      NOT NULL DEFAULT 'отправлена',
    PRIMARY KEY (supplier_order_id),
    CONSTRAINT fk_so_supplier FOREIGN KEY (supplier_id)
        REFERENCES suppliers (supplier_id) ON DELETE RESTRICT ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ----------------------------------------------------------------------------
-- 13. Позиции заявок поставщикам
-- Отклонение от примерной структуры: удалён атрибут total_amount —
-- вычислимое значение (quantity * price_per_unit); хранение вычислимых
-- атрибутов нарушает 3НФ. Позиция содержит либо лекарство, либо ингредиент
-- (CHECK-ограничение).
-- ----------------------------------------------------------------------------
CREATE TABLE supplier_order_items (
    order_item_id     INT UNSIGNED NOT NULL AUTO_INCREMENT,
    supplier_order_id INT UNSIGNED NOT NULL,
    medicine_id       INT UNSIGNED NULL,
    ingredient_id     INT UNSIGNED NULL,
    quantity          DECIMAL(12,3) NOT NULL,
    price_per_unit    DECIMAL(10,2) NOT NULL,
    PRIMARY KEY (order_item_id),
    CONSTRAINT fk_soi_order FOREIGN KEY (supplier_order_id)
        REFERENCES supplier_orders (supplier_order_id) ON DELETE CASCADE ON UPDATE CASCADE,
    -- ON UPDATE RESTRICT: см. комментарий к fk_inventory_medicine
    CONSTRAINT fk_soi_medicine FOREIGN KEY (medicine_id)
        REFERENCES medicines (medicine_id) ON DELETE RESTRICT ON UPDATE RESTRICT,
    CONSTRAINT fk_soi_ingredient FOREIGN KEY (ingredient_id)
        REFERENCES ingredients (ingredient_id) ON DELETE RESTRICT ON UPDATE RESTRICT,
    CONSTRAINT chk_soi_target CHECK (
        (medicine_id IS NULL AND ingredient_id IS NOT NULL) OR
        (medicine_id IS NOT NULL AND ingredient_id IS NULL)
    )
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============================================================================
-- Служебные таблицы веб-сервиса
-- ============================================================================

-- ----------------------------------------------------------------------------
-- 14. Пользователи веб-сервиса (аутентификация)
-- Пароль хранится как SHA-256(соль + пароль); соль индивидуальна.
-- ----------------------------------------------------------------------------
CREATE TABLE users (
    user_id       INT UNSIGNED NOT NULL AUTO_INCREMENT,
    first_name    VARCHAR(100) NOT NULL,
    last_name     VARCHAR(100) NOT NULL,
    patronymic    VARCHAR(100) NOT NULL,
    email         VARCHAR(150) NOT NULL,
    password_hash CHAR(64) NOT NULL,                       -- SHA-256 в hex
    salt          CHAR(32) NOT NULL,                       -- 128 бит в hex
    birth_date    DATE NOT NULL,
    created_at    DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (user_id),
    UNIQUE KEY uq_users_email (email)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ----------------------------------------------------------------------------
-- 15. Сессии (cookie-токены авторизации)
-- ----------------------------------------------------------------------------
CREATE TABLE sessions (
    session_id INT UNSIGNED NOT NULL AUTO_INCREMENT,
    user_id    INT UNSIGNED NOT NULL,
    token      CHAR(32) NOT NULL,                          -- 128 бит из /dev/urandom в hex
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at DATETIME NOT NULL,
    PRIMARY KEY (session_id),
    UNIQUE KEY uq_sessions_token (token),
    CONSTRAINT fk_sessions_user FOREIGN KEY (user_id)
        REFERENCES users (user_id) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ----------------------------------------------------------------------------
-- 16. Загруженные изображения (модуль «Добавление изображения»)
-- Служебная таблица добавлена к 13 предметным: по заданию после загрузки
-- в таблицу выводится изображение и дата его загрузки, а пути к файлам
-- обязаны храниться в БД.
-- ----------------------------------------------------------------------------
CREATE TABLE uploads (
    upload_id     INT UNSIGNED NOT NULL AUTO_INCREMENT,
    user_id       INT UNSIGNED NOT NULL,
    file_path     VARCHAR(255) NOT NULL,                   -- путь к файлу на сервере
    original_name VARCHAR(255) NOT NULL,                   -- исходное имя файла (только для показа)
    uploaded_at   DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (upload_id),
    CONSTRAINT fk_uploads_user FOREIGN KEY (user_id)
        REFERENCES users (user_id) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
