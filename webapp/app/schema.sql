drop table if exists config;
create table config (
  `id` integer primary key autoincrement,
  `timestamp` timestamp,
  `ackTimestamp` timestamp,
  `battery` varchar(32),
  `manualTimeout` unsigned integer,
  `pump0` boolean,
  `pump1` boolean,
  `pump2` boolean,
  `pump3` boolean,
  `targetFlow` unsigned integer,
  `targetLevel1` unsigned integer,
  `targetLevel2` unsigned integer,
  `targetLevel3` unsigned integer,
  `targetLevel4` unsigned integer,
  `minLevel1` unsigned integer,
  `minLevel2` unsigned integer,
  `minLevel3` unsigned integer,
  `minLevel4` unsigned integer,
  `maxLevel1` unsigned integer,
  `maxLevel2` unsigned integer,
  `maxLevel3` unsigned integer,
  `maxLevel4` unsigned integer
);

drop table if exists status;
create table status (
  `id` integer primary key autoincrement,
  `timestamp` timestamp,
  `battery` varchar(16),
  `panic` boolean,
  `manualTimeout` unsigned integer,
  `pump0` unsigned integer,
  `pump1` unsigned integer,
  `pump2` unsigned integer,
  `pump3` unsigned integer,
  `targetFlow` unsigned integer,
  `flowIn` unsigned integer,
  `flowOut` unsigned integer,
  `currentLevel1` unsigned integer,
  `currentLevel2` unsigned integer,
  `currentLevel3` unsigned integer,
  `targetLevel1` unsigned integer,
  `targetLevel2` unsigned integer,
  `targetLevel3` unsigned integer,
  `minLevel1` unsigned integer,
  `minLevel2` unsigned integer,
  `minLevel3` unsigned integer,
  `maxLevel1` unsigned integer,
  `maxLevel2` unsigned integer,
  `maxLevel3` unsigned integer
);
