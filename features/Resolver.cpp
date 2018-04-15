#include "Resolver.hpp"

#include "../Options.hpp"
#include "../helpers/Math.hpp"

#include "LagCompensation.hpp"


void Resolver::Resolve(C_BasePlayer *player)
{
	if (!Global::userCMD)
		return;

	if (player->m_iTeamNum() == g_LocalPlayer->m_iTeamNum()) return;

	int idx = player->EntIndex();
	m_arrInfos[idx].curTickRecord.SaveRecord(player);

	if (m_arrInfos[idx].arr_tickRecords.size() > 16)
		m_arrInfos[idx].arr_tickRecords.pop_back();

	m_arrInfos[idx].arr_tickRecords.emplace_back(m_arrInfos[idx].curTickRecord);


	StartResolving(player);


	m_arrInfos[idx].prevTickRecord = m_arrInfos[idx].curTickRecord;
}

void Resolver::StartResolving(C_BasePlayer *player)
{
	int idx = player->EntIndex();
	float new_yaw = player->m_flLowerBodyYawTarget();
	float new_pitch = player->m_angEyeAngles().pitch;
	auto cur = m_arrInfos.at(player->EntIndex()).m_sRecords;

	if (cur.size() < 2)
		return;

	AnimationLayer curBalanceLayer, prevBalanceLayer;

	SResolveInfo &player_recs = m_arrInfos[idx];

	if (player_recs.prevTickRecord.m_flVelocity == 0.0f && player_recs.curTickRecord.m_flVelocity != 0.0f)
		Math::VectorAngles(player_recs.curTickRecord.m_vecVelocity, player_recs.m_angDirectionFirstMoving);

	if (g_Options.hvh_resolver_override && g_InputSystem->IsButtonDown(g_Options.hvh_resolver_override_key))
	{
		new_yaw = player->m_flLowerBodyYawTarget();

		switch (g_Options.hvh_resolver_override_options)
		{
		case 0:
			new_yaw += 180.f;
			m_arrInfos[player->EntIndex()].resolvemode = "Override: Inverse";
			break;
		case 1:
			new_yaw = moving_vals[player->EntIndex()];
			m_arrInfos[player->EntIndex()].resolvemode = "Override: Last Move";
			break;
		case 2:
			new_yaw = storedlby[player->EntIndex()];
			m_arrInfos[player->EntIndex()].resolvemode = "Override: Last Real";
			break;
		}

		new_yaw = Math::ClampYaw(new_yaw);
		player->m_angEyeAngles().yaw = new_yaw;
		player->SetPoseAngles(new_yaw, player->m_angEyeAngles().pitch);
		player->SetAbsAngles(QAngle(0, new_yaw, 0));
		player->m_angRotation() = QAngle(0, 0, 0);
		player->m_angAbsRotation() = QAngle(0, 0, 0);
		return;
	}
	else if (g_Options.hvh_resolver_type == 0)
	{
		if (IsEntityMoving(player))
		{
			if (IsFakewalking(player, player_recs.curTickRecord)) {

				
					new_yaw = player_recs.m_angDirectionFirstMoving.yaw + 180.f;
					m_arrInfos[player->EntIndex()].resolvemode = "Fakewalk: Inverse";
				
			}
			else {
				new_yaw = player->m_flLowerBodyYawTarget();
				m_arrInfos[player->EntIndex()].resolvemode = "Moving: LBY";
				moving_vals[player->EntIndex()] = player->m_flLowerBodyYawTarget();
				moving_velocity[player->EntIndex()] = player->m_vecVelocity();
				storedlby[player->EntIndex()] = player->m_flLowerBodyYawTarget();
			}
		}
		else if (!player->CheckOnGround())
		{
			
			new_yaw = moving_vals[player->EntIndex()];
			
		}
		else if (IsAdjustingBalance(player, player_recs.curTickRecord, &curBalanceLayer))
		{
			if (IsAdjustingBalance(player, player_recs.prevTickRecord, &prevBalanceLayer))
			{
				if ((prevBalanceLayer.m_flCycle != curBalanceLayer.m_flCycle) || curBalanceLayer.m_flWeight == 1.f)
				{
					float
						flAnimTime = curBalanceLayer.m_flCycle,
						flSimTime = player->m_flSimulationTime();

					if (flAnimTime < 0.01f && prevBalanceLayer.m_flCycle > 0.01f && g_Options.rage_lagcompensation && CMBacktracking::Get().IsTickValid(TIME_TO_TICKS(flSimTime - flAnimTime)))
					{
						CMBacktracking::Get().SetOverwriteTick(player, QAngle(player->m_angEyeAngles().pitch, player->m_flLowerBodyYawTarget(), 0), (flSimTime - flAnimTime), 2);
						m_arrInfos[player->EntIndex()].resolvemode = "Standing: LBY Backtrack";
						storedlby[player->EntIndex()] = player->m_flLowerBodyYawTarget();
					}
					else
					{
						new_yaw = moving_vals[player->EntIndex()];
					}
				}
				else if (curBalanceLayer.m_flWeight == 0.f && (prevBalanceLayer.m_flCycle > 0.92f && curBalanceLayer.m_flCycle > 0.92f)) // breaking lby with delta < 120/supressing
				{
					new_yaw = moving_vals[player->EntIndex()];
				}
			}
			else
			{
				new_yaw = moving_vals[player->EntIndex()];
			}
		}
		else
		{
			new_yaw = moving_vals[player->EntIndex()];
		}

		new_yaw = Math::ClampYaw(new_yaw);
		player->m_angEyeAngles().yaw = new_yaw;
		player->m_angEyeAngles().pitch = new_pitch;
		player->SetPoseAngles(new_yaw, new_pitch);
		player->SetAbsAngles(QAngle(0, new_yaw, 0));
		player->m_angRotation() = QAngle(0, 0, 0);
		player->m_angAbsRotation() = QAngle(0, 0, 0);
	}
}

STickRecord Resolver::GetLatestUpdateRecord(C_BasePlayer *player) {
	if (m_arrInfos[player->EntIndex()].arr_tickRecords.size())
		return *m_arrInfos[player->EntIndex()].arr_tickRecords.begin();
	return STickRecord();
}


bool Resolver::IsEntityMoving(C_BasePlayer *player)
{
	return (player->m_vecVelocity().Length2D() > 0.1f && player->m_fFlags() & FL_ONGROUND);
}

bool Resolver::IsFakewalking(C_BasePlayer *player, STickRecord &record)
{
	

	return false; //:thinking:
}

bool Resolver::IsAdjustingBalance(C_BasePlayer *player, STickRecord &record, AnimationLayer *layer)
{
	for (int i = 0; i < record.m_iLayerCount; i++)
	{
		const int activity = player->GetSequenceActivity(record.animationLayer[i].m_nSequence);
		if (activity == 979)
		{
			*layer = record.animationLayer[i];
			return true;
		}
	}
	return false;
}
