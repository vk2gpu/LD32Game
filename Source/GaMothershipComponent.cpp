#include "GaMothershipComponent.h"
#include "GaAsteroidComponent.h"
#include "GaAsteroidFieldComponent.h"
#include "GaMinerComponent.h"
#include "GaUnitComponent.h"


#include "System/Scene/Rendering/ScnCanvasComponent.h"
#include "System/Scene/Rendering/ScnDebugRenderComponent.h"
#include "System/Scene/Rendering/ScnFont.h"
#include "System/Scene/Rendering/ScnViewComponent.h"
#include "System/Scene/Physics/ScnPhysicsRigidBodyComponent.h"
#include "System/Scene/Physics/ScnPhysicsEvents.h"

#include "Base/BcRandom.h"

//////////////////////////////////////////////////////////////////////////
// Define resource internals.
REFLECTION_DEFINE_DERIVED( GaMothershipComponent );

void GaMothershipComponent::StaticRegisterClass()
{
	ReField* Fields[] = 
	{
		new ReField( "MinerEntity_", &GaMothershipComponent::MinerEntity_, bcRFF_SHALLOW_COPY | bcRFF_IMPORTER ),
		new ReField( "TotalResources_", &GaMothershipComponent::TotalResources_, bcRFF_IMPORTER ),

		new ReField( "TargetPosition_", &GaMothershipComponent::TargetPosition_, bcRFF_TRANSIENT ),
		new ReField( "TargetRotation_", &GaMothershipComponent::TargetRotation_, bcRFF_TRANSIENT ),
		new ReField( "Asteroids_", &GaMothershipComponent::Asteroids_, bcRFF_TRANSIENT ),
		new ReField( "Miners_", &GaMothershipComponent::Miners_, bcRFF_TRANSIENT ),

		new ReField( "AttackWeight_", &GaMothershipComponent::AttackWeight_, bcRFF_TRANSIENT ),
		new ReField( "MineWeight_", &GaMothershipComponent::MineWeight_, bcRFF_TRANSIENT ),
		new ReField( "BuildWeight_", &GaMothershipComponent::BuildWeight_, bcRFF_TRANSIENT ),
		new ReField( "RepairWeight_", &GaMothershipComponent::RepairWeight_, bcRFF_TRANSIENT ),
		new ReField( "ReturnWeight_", &GaMothershipComponent::ReturnWeight_, bcRFF_TRANSIENT ),
	};
	ReRegisterClass< GaMothershipComponent, Super >( Fields )
		.addAttribute( new ScnComponentAttribute( 0 ) );
}

//////////////////////////////////////////////////////////////////////////
// Ctor
GaMothershipComponent::GaMothershipComponent():
	MinerEntity_( nullptr ),
	TotalResources_( 0.0f ),
	TotalHull_( 100.0f ),
	Canvas_( nullptr ),
	Font_( nullptr ),
	View_( nullptr ),
	AttackWeight_( 0.0f ),
	MineWeight_( 0.0f ),
	BuildWeight_( 0.0f ),
	RepairWeight_( 0.0f ),
	ReturnWeight_( 0.0f )
{

}

//////////////////////////////////////////////////////////////////////////
// Dtor
//virtual
GaMothershipComponent::~GaMothershipComponent()
{

}

//////////////////////////////////////////////////////////////////////////
// update
void GaMothershipComponent::update( BcF32 Tick )
{
	auto RigidBody = getComponentByType< ScnPhysicsRigidBodyComponent >();
	auto Unit = getComponentByType< GaUnitComponent >();

	// Stabilise ship position.
	auto RigidBodyPosition = RigidBody->getPosition();
	auto Displacement = TargetPosition_ - RigidBodyPosition;
	RigidBody->applyCentralForce( Displacement * RigidBody->getMass() );

	// Stabilise ship rotation.
	auto RigidBodyRotation = RigidBody->getRotation();
	RigidBodyRotation.inverse();
	MaQuat RotationDisplacement = TargetRotation_ * RigidBodyRotation;
	auto EularRotationDisplacement = RotationDisplacement.asEuler();
	RigidBody->applyTorque( EularRotationDisplacement * RigidBody->getMass() * 10.0f );

	// Dampen.
	RigidBody->setLinearVelocity( RigidBody->getLinearVelocity() * 0.99f );
	RigidBody->setAngularVelocity( RigidBody->getAngularVelocity() * 0.99f );

	// Random impulse just to make it look a bit more funky.
	if( BcRandom::Global.randRealRange( 0.0f, 100.0f ) < 1.0f )
	{
		RigidBody->applyImpulse( 
			MaVec3d( 
				BcRandom::Global.randRealRange( -1.0f, 1.0f ),
				BcRandom::Global.randRealRange( -1.0f, 1.0f ),
				BcRandom::Global.randRealRange( -1.0f, 1.0f ) ) * RigidBody->getMass() * 0.1f,
			MaVec3d( 
				BcRandom::Global.randRealRange( -1.0f, 1.0f ),
				BcRandom::Global.randRealRange( -1.0f, 1.0f ),
				BcRandom::Global.randRealRange( -1.0f, 1.0f ) ) * 0.5f );
	}

	// AI player :)
	if( Unit->getTeam() == 2 )
	{
		AttackWeight_ = std::max( AttackWeight_, 0.0f );
		MineWeight_ = std::max( MineWeight_, 0.0f );
		BuildWeight_ = std::max( BuildWeight_, 0.0f );
		RepairWeight_ = std::max( RepairWeight_, 0.0f );
		ReturnWeight_ = std::max( RepairWeight_, 0.0f );

		// Build miners.
		{
			for( BcU32 Idx = 1; Idx < 8; ++Idx )
			{
				if( TotalResources_ >= ( 100.0f * BcF32( Idx ) ) && Miners_.size() < ( 2 * Idx ) )
				{
					GaUnitActionEvent Event;
					Event.SourceUnit_ = Unit;
					Event.TargetUnit_ = Unit;
					getParentEntity()->publish( 0, Event );
					MineWeight_ += Tick;
					break;
				}
			}
		}

		// Control miners.
		BcU32 CurrMiner = 0;
		for( auto Miner : Miners_ )
		{
			// Use half of our miners for defence if we have enough money to replace 2 of em.
			if( CurrMiner < Miners_.size() / 2  || TotalResources_ <= 100.0f )
			{
				// Send to mine if idle.
				if( Miner->isIdle() && !Miner->isFull( 1.0f ) )
				{
					auto Asteroid = findAsteroid( 
						[ Miner ]( GaAsteroidComponent* Asteroid )
						{
							auto Distance = ( Miner->getPosition() - Asteroid->getPosition() ).magnitude();
							auto Z = Asteroid->getPosition().z();
							if( Z < -24.0f || Z > 24.0f )
							{
								Z += 1e12f;
							}

							return -Distance - Z;
						} );

					if( Asteroid != nullptr )
					{
						auto Z = Asteroid->getPosition().z();
						if( Z > -24.0f && Z < 24.0f )
						{
							GaUnitActionEvent Event;
							Event.SourceUnit_ = Miner->getComponentByType< GaUnitComponent >();
							Event.TargetUnit_ = Asteroid->getComponentByType< GaUnitComponent >();
							Miner->getParentEntity()->publish( 0, Event );
							ReturnWeight_ += Tick;
						}
					}
				}
				// Get returning if almost full.
				else if( Miner->isIdle() && Miner->isFull( 1.0f ) )
				{
					GaUnitActionEvent Event;
					Event.SourceUnit_ = Miner->getComponentByType< GaUnitComponent >();
					Event.TargetUnit_ = Unit;
					Miner->getParentEntity()->publish( 2, Event );
					ReturnWeight_ += Tick;
				}
			}
			else
			{
				// If we have an idle miner and enough to replace it.
				if( Miner->isIdle() )
				{
					// Crash into nearest asteroid.
					auto Asteroid = findAsteroid( 
						[ this ]( GaAsteroidComponent* Asteroid )
						{
							auto Distance = ( Enemy_->getComponentByType< ScnPhysicsRigidBodyComponent >()->getPosition() - Asteroid->getPosition() ).magnitude();
							auto Z = Asteroid->getPosition().z();
							if( Z < -24.0f || Z > 24.0f )
							{
								Z += 1e12f;
							}

							return -Distance - Z;
						} );

					if( Asteroid != nullptr )
					{
						auto Z = Asteroid->getPosition().z();
						if( Z > -24.0f && Z < 24.0f )
						{
							GaUnitActionEvent Event;
							Event.SourceUnit_ = Miner->getComponentByType< GaUnitComponent >();
							Event.TargetUnit_ = Asteroid->getComponentByType< GaUnitComponent >();
							Miner->getParentEntity()->publish( 1, Event );
							ReturnWeight_ += Tick;
						}
					}
				}
			}

			++CurrMiner;
		}
	}

	// HUD crap.
	{
		auto Position = View_->getScreenPosition( getParentEntity()->getWorldPosition() );
		Position += MaVec2d( 0.0f, -128.0f );

		ScnFontDrawParams DrawParams = ScnFontDrawParams()
			.setSize( 24.0f )
			.setMargin( 8.0f )
			.setTextColour( RsColour::WHITE )
			.setAlignment( ScnFontAlignment::HCENTRE | ScnFontAlignment::VCENTRE );
		
		BcChar Buffer[ 1024 ];
		BcSPrintf( Buffer, "R: $%.2f\nH: %.2f%%", TotalResources_, TotalHull_ );

		Font_->drawText( Canvas_, DrawParams, Position, MaVec2d( 0.0f, 0.0f ), Buffer );

	}
}

//////////////////////////////////////////////////////////////////////////
// update
void GaMothershipComponent::onAttach( ScnEntityWeakRef Parent )
{
	// Build
	Parent->subscribe( 0, this,
		[ this ]( EvtID, const EvtBaseEvent& BaseEvent )
		{
			const auto& Event = BaseEvent.get< GaUnitActionEvent >();
			if( Event.SourceUnit_ != Event.TargetUnit_ )
			{
				return evtRET_PASS;
			}

			if( subResources( 50.0f ) )
			{
				PSY_LOG( "GaMothershipComponent: Build event." );

				MaMat4d MinerTransform0 = getParentEntity()->getWorldMatrix();
				MinerTransform0.translation( MinerTransform0.translation() + MaVec3d( 0.0f, 5.0f, 0.0f ) );

				BcAssert( MinerEntity_ );
				auto MinerEntity0 = ScnCore::pImpl()->spawnEntity(
				ScnEntitySpawnParams(
					"MinerEntity_0",
					MinerEntity_,
					MinerTransform0,
					getParentEntity()->getParentEntity() ) );

				// Set same team.
				auto MinerUnit = MinerEntity0->getComponentByType< GaUnitComponent >();
				MinerUnit->setTeam( getComponentByType< GaUnitComponent >()->getTeam() );

				auto MinerComponent = MinerEntity0->getComponentByType< GaMinerComponent >();
				BcAssert( MinerComponent );
				MinerComponent->addNotifier( this );
				Miners_.push_back( MinerComponent );
			}
			else
			{
				// EEE ERR.
			}
			return evtRET_PASS;
		} );

	// Repair
	Parent->subscribe( 1, this,
		[ this ]( EvtID, const EvtBaseEvent& BaseEvent )
		{
			const auto& Event = BaseEvent.get< GaUnitActionEvent >();
			if( Event.SourceUnit_ != Event.TargetUnit_ )
			{
				return evtRET_PASS;
			}

			if( TotalHull_ < 100.0f && subResources( 50.0f ) )
			{
				PSY_LOG( "GaMothershipComponent: Repair event." );
				TotalHull_ += 10.0f;

				TotalHull_ = std::min( 100.0f, TotalHull_ );
			}

			return evtRET_PASS;
		} );

	// Collision.
	Parent->subscribe( (EvtID)ScnPhysicsEvents::COLLISION, this,
		[ this, Parent ]( EvtID, const EvtBaseEvent& BaseEvent )
		{
			const auto& Event = BaseEvent.get< ScnPhysicsEventCollision >();

			auto OtherEntity = Event.BodyB_->getParentEntity();
			auto AsteroidComponent = OtherEntity->getComponentByType< GaAsteroidComponent >();
			if( AsteroidComponent )
			{
				auto Damage = 10.0f * AsteroidComponent->getSize();
				PSY_LOG( "GaMothershipComponent: Collision. Take damage: %f", Damage );
				TotalHull_ = std::max( 0.0f, TotalHull_ - Damage );

				if( TotalHull_ <= 0.0f )
				{
					// TODO: Win/lose.
				}

				AsteroidComponent->recycle();
			}
			return evtRET_PASS;
		} );

	for( auto Entity : getParentEntity()->getParentEntity()->getComponents() )
	{
		// Get enemy mothership.
		auto MothershipComponent = Entity->getComponentByType< GaMothershipComponent >();
		if( MothershipComponent )
		{
			if( MothershipComponent->getComponentByType< GaUnitComponent >()->getTeam() !=
				getComponentByType< GaUnitComponent >()->getTeam() )
			{
				Enemy_ = MothershipComponent;
			}
		}

		// Check for asteroids in parent.
		auto FieldComponent = Entity->getComponentByType< GaAsteroidFieldComponent >();
		if( FieldComponent )
		{
			for( auto Component : FieldComponent->getParentEntity()->getComponents() )
			{
				auto Asteroid = Component->getComponentByType< GaAsteroidComponent >();
				if( Asteroid )
				{
					Asteroids_.push_back( Asteroid );
				}
			}
		}
	}

	PSY_LOG( "Added %u asteroids to mothership.", Asteroids_.size() );

	// Cache transforms.
	auto WorldMatrix = Parent->getWorldMatrix();
	TargetPosition_ = WorldMatrix.translation();
	TargetRotation_.fromMatrix4d( WorldMatrix );

	//
	Canvas_ = getComponentAnyParentByType< ScnCanvasComponent >();
	BcAssert( Canvas_ );
	Font_ = getComponentAnyParentByType< ScnFontComponent >();
	BcAssert( Font_ );
	View_ = ScnCore::pImpl()->findEntity( "GameEntity" )->getComponentAnyParentByType< ScnViewComponent >();
	BcAssert( View_ );

	Super::onAttach( Parent );
}

//////////////////////////////////////////////////////////////////////////
// onObjectDeleted
void GaMothershipComponent::onObjectDeleted( class ReObject* Object )
{
	auto FoundMiner = std::find_if( Miners_.begin(), Miners_.end(),
		[ Object ]( GaMinerComponent* Miner )
		{
			return Object == Miner;
		} );
	auto FoundAsteroid = std::find_if( Asteroids_.begin(), Asteroids_.end(),
		[ Object ]( GaAsteroidComponent* Asteroid )
		{
			return Object == Asteroid;
		} );

	if( FoundMiner != Miners_.end() )
	{
		Miners_.erase( FoundMiner );
	}
	if( FoundAsteroid != Asteroids_.end() )
	{
		Asteroids_.erase( FoundAsteroid );
	}
}

//////////////////////////////////////////////////////////////////////////
// addResources
void GaMothershipComponent::addResources( BcF32 Resources )
{
	TotalResources_ += Resources;
}

//////////////////////////////////////////////////////////////////////////
// subResources
BcBool GaMothershipComponent::subResources( BcF32 Resources )
{
	if( TotalResources_ >= Resources )
	{
		TotalResources_ -= Resources;
		TotalResources_ = std::max( TotalResources_, 0.0f );
		return BcTrue;
	}
	return BcFalse;
}

//////////////////////////////////////////////////////////////////////////
// findNearestAsteroid
class GaAsteroidComponent* GaMothershipComponent::findNearestAsteroid( const MaVec3d& Position )
{
	class GaAsteroidComponent* Nearest = nullptr;
	BcF32 NearestDistance = 1e6f;

	for( auto Asteroid : Asteroids_ )
	{
		auto DistSqr = ( Asteroid->getParentEntity()->getWorldPosition() - Position ).magnitudeSquared();
		if( DistSqr < NearestDistance )
		{
			Nearest = Asteroid;
			NearestDistance = DistSqr;
		}
	}

	return Nearest;
}

//////////////////////////////////////////////////////////////////////////
// findAsteroid
class GaAsteroidComponent* GaMothershipComponent::findAsteroid( std::function< BcF32( class GaAsteroidComponent* ) > HeuristicFunc )
{
	class GaAsteroidComponent* Best = nullptr;
	BcF32 BestHeuristic = -1e6f;

	for( auto Asteroid : Asteroids_ )
	{
		auto H = HeuristicFunc( Asteroid );
		if( H > BestHeuristic )
		{
			Best = Asteroid;
			BestHeuristic = H;
		}
	}

	return Best;
}
