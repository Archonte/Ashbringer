ALTER TABLE creature_template
  ADD COLUMN `spell5` mediumint(8) unsigned NOT NULL default '0' AFTER `spell4`,
  ADD COLUMN `spell6` mediumint(8) unsigned NOT NULL default '0' AFTER `spell5`;

ALTER TABLE creature_template
  ADD COLUMN `VehicleId` mediumint(8) unsigned NOT NULL default '0' AFTER `PetSpellDataId`;

DROP TABLE IF EXISTS `vehicle_accessory`;
CREATE TABLE `vehicle_accessory` (
  `entry` MEDIUMINT(8) UNSIGNED NOT NULL DEFAULT 0,
  `accessory_entry` MEDIUMINT(8) UNSIGNED NOT NULL DEFAULT 0,
  `seat_id` TINYINT(1) SIGNED NOT NULL DEFAULT 0,
  `minion` TINYINT(1) UNSIGNED NOT NULL DEFAULT 0,
  `description` TEXT NOT NULL,
  PRIMARY KEY (`entry`, `seat_id`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8 ROW_FORMAT=FIXED COMMENT='Vehicle Accessory System';
