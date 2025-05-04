-- Resources
INSERT INTO resources (id, name, unit) VALUES
(1, 'Money', 'credits'),
(2, 'Coal', 'kg');
-- Dummy régiók létrehozása a hivatkozásokhoz
INSERT INTO regions (id, lat, lon, lat2, lon2, elevation, population, light_pollution, name) VALUES
(101, 48.1, 16.3, 48.2, 16.4, 250.0, 1000, 2.5, 'DemoRegion1'),
(102, 48.2, 16.5, 48.3, 16.6, 180.0, 800, 1.9, 'DemoRegion2');
-- User regions
INSERT INTO user_regions (user_id, region_id) VALUES
(2, 101),
(3, 102);

-- Region resources
INSERT INTO region_resources (region_id, resource_id, quantity, price_buy, price_sell) VALUES
(101, 1, 1500, 0.9, 1.2),
(101, 2, 500, 2.0, 2.5),
(102, 1, 2000, 1.0, 1.4),
(102, 2, 300, 1.8, 2.2);

-- Shapes
INSERT INTO shapes (id, name, type_id) VALUES
(1, 'WorkerMesh', 1),
(2, 'SoldierMesh', 1);

-- Entities
INSERT INTO entities (id, user_id, type_id, name, pos_lat, pos_lon, altitude, heading, status_id, shape_id) VALUES
(1001, 2, 1, 'WorkerA', 48.1, 16.3, 0, 0, 1, 1),
(1002, 2, 1, 'SoldierA', 48.1, 16.4, 0, 0, 2, 2),
(1003, 3, 1, 'WorkerB', 48.2, 16.5, 0, 0, 3, 1),
(1004, 3, 1, 'DeadOne', 48.2, 16.6, 0, 0, 5, 2);

-- Persons
INSERT INTO persons (entity_id, job_id, health, morale) VALUES
(1001, 2, 100, 90),
(1002, 3, 100, 80),
(1003, 2, 95, 100),
(1004, 3, 0, 0);