# Names and comments to send upstream

Names and facts worked out while building the port and the netcode, not yet on upstream master. Each entry says
where the evidence is. When a batch is ready it goes to a branch off upstream master (names and comments only, VC6
`build.py` and `compare_builds.py` unchanged), as the first batch did (PR #867).

## Candidates

### `power_up_indices` (`Source/Player.hpp`)

`Player::CollectPowerUp_564D60` shows what the unnamed ones do:

| Now | Proposed | Evidence |
|---|---|---|
| `Unk_0` | `Multiplier_0` | `ChangeMultipliers_4766B0(1)`; refused at 99 multipliers |
| `Unk_1` | `ExtraLife_1` | `ChangeLifeCountByAmount_5699F0(1)`; refused at 99 lives |
| `Unk_2` | `Health_2` | `SetFullHealth_4A5050()`; refused at full health |
| `Unk_5` | `CopBribe_5` | `ClearWantedPoints_420B80()`; refused without wanted points |
| `Unk_10` | `Respect_10` | `IncrementRespect_4BEE50(idx, 20)` with the gang of the zone the player is in |

`Unk_13` to `Unk_16` only set their timer to 1 (the `default:` case); what reads them is still open.

### `objectives_enum`

| Now | Proposed | Evidence |
|---|---|---|
| `objective_32` | `pull_target_out_of_car_32` | `Ped::PullDriverOutOfCar_46A1F0` runs it; its internal state machine is `Ped::PullDriverOutOfCarStateMachine_46B2F0`, which walks the ped's char to a door of the target ped's car. Police crews give it when chasing on foot (`PoliceCrew::State5_PursueOrChase_572920`), and a group's new leader inherits it (`PedGroup::PromoteMemberToLeader_4C9680`). |
| `objective_54` | `call_police_car_54` | `Ped::CallPoliceCar_469FE0` runs it. `Player::IsBustedObjective54_476730` tells a busted player from a wasted one by it, so it is the busted player's ped being taken away (to check). |

### Comments

- `ped_state_1::entering_car_3` with `ped_state_2::ped2_following_a_car_4`: the ped is walking after the car it wants to get into, while the car still moves or the two don't overlap yet (`Ped.cpp`, the enter-car state machine around `IsOtherPedEnteringAsDriver_46BD50`). Only `ped2_entering_a_car_6` is at the door.
- `PedGroup::PromoteMemberToLeader_4C9680` gives the new leader the group's objective, whatever the new leader is doing (in a car too).
- `Ped::PullDriverOutOfCarStateMachine_46B2F0` assumes the ped is on foot (it uses `field_168_game_object` without a check).
