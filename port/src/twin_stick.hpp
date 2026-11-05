// CONTROLS TWIN STICK, on foot: the left stick or the movement keys walk and the right stick or the mouse
// aims, both relative to the screen. The game's controls do the rest: mTurnSpeed turns the ped to the aim
// and mMoveDirection walks it, in a direction of its own (Walk).
#pragma once

#include "ang16.hpp"

class Camera;
class Car;
class Char;
class Ped;
class Player;

namespace PortTwinStick
{
// At the end of Player::DoPedControlInputs.
void PedControls(Player* pPlayer, Ped* pPed);
// At the start of Char::UpdateWalking: true if this step walks along the move, the facing to end with in *pFacing.
bool Walk(Char* pChar, Ang16* pFacing);
// In Car::HandleUserInput, unless SPECIAL turns the roof gun: the player's fire truck, tank or gun jeep
// turns its roof gun toward the aim.
void AimTurret(Car* pCar);
// With the HUD.
void DrawReticle();
// AIM CAMERA: after Camera::ApplyPanOffset in Camera::UpdateFollowPedCamera, the target moved toward the aim.
void AimCamera(Camera* pCamera, Ped* pPed, Fix16& x, Fix16& y);
// The aim camera's look-ahead is on the view (gone back to 0 once it is off).
bool Looking();
} // namespace PortTwinStick
