# Items (`data/scripts/inventory/*.scr` and others under `data/scripts/`)

Inventory items are declared in text scripts (the big ones are generated from a spreadsheet: `inventory.scr`, `inventory_gen.scr`, `inventory_legendary.scr`, `inventory_special.scr`, `collectables.scr`, DLC variants). Parser: `src/core/script` -> `src/core/items.{hpp,cpp}` (`collectItems`); `oc_itemstat <DW dir> [item id]` prints totals or dumps one item.

```
Item("Ammo_PistolBig", CategoryType_Ammo)       // id, category identifier
{
    AmmoCount(30); BulletId(Bullet_Pistol); Color(Color_Green); HudIcon(ammo_pistol);
    ItemType(ItemType_Ammo); MaxStackCount(99); Mesh("loot_ok_ammo_short.msh");
    PhysicsScript("single_dropped_inventory.phx"); Price(700); Skin("Default"); Visibility(true);
}
Item("Name", "OtherItem") { ... }                // second argument a string: derived from another item (3679 of them)
```

Counts over all files: 7606 `Item` blocks, 6281 distinct ids (1325 ids are defined more than once, later definitions override or extend earlier ones; the override order is not determined yet), 22 category types: Melee 1776, Collectable 841, Throwable 377, ItemBundle 258, Firearm 232, Voucher 109, VehicleUpgrade 59, Ammo 57, CraftComponent 45, Powerup 39, Medkit 20, and a few small ones.

Most common properties: `ItemType`, `Mesh`, `Color`, `GameVersion`, `Condition`, `DamageType`, `HudIcon`, `CriticalProb`, `Skin`, `UpgradeLevel`, `Price`, `InventoryMeshHq`, `RepairPart`, `Visibility`, `PhysicsScript`, `Name`, `CutTypesGroup`, `RequiredItem`, `AnimPrefix`. `Mesh` names resolve to `.msh` resources in the packs and `Skin` to a material skin of that mesh, so items can be shown with the existing viewer pipeline.

Open: resolution of derived items (property inheritance and override order), meaning of `Condition`/`UpgradeLevel` chains, and how `Name` / `Description` text keys map to localisation tables.
