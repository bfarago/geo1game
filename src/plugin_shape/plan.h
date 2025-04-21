#ifndef PLAN_H
#define PLAN_H

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_OBJECT_PLANS 100

typedef enum {
    ShipFishing,
    ShipCargo,
    ShipPassenger,
    ShipMilitary,
    ShipAircraftCarrier,
    PlanePassenger,
    PlaneMilitary,
    PlaneBalloon,
    VehicleCar,
    VehicleTruck,
    VehicleBus,
    VehicleTank,
    MAX_OBJECT_TYPES
} ObjectTypeId;

/**
 * * PlanConfigValue structure
 * * This structure holds the minimum and delta values for a plan configuration.
 */
typedef struct{
    float min;
    float delta;
} PlanConfigValue;

/**
 * * PlanConfigUnion structure
 * * This union holds the configuration values for different object types.
 */
typedef struct ShipPlanConfig_s{
    PlanConfigValue Len;
    PlanConfigValue Width;
    PlanConfigValue Height;
    PlanConfigValue speed;
    PlanConfigValue maxspeed;
    PlanConfigValue maxaccel;
    PlanConfigValue maxdecel;
    PlanConfigValue maxturn;
    PlanConfigValue maxturnrate;
    PlanConfigValue maxturnangle;
    PlanConfigValue maxpitch;
    PlanConfigValue maxpitchrate;
} ShipPlanConfig;
typedef struct PlanePlanConfig_s{
    PlanConfigValue WingAngle;
    PlanConfigValue WingSpan; 
} PlanePlanConfig;
typedef struct VehiclePlanConfig_s{
    PlanConfigValue speed;
    PlanConfigValue maxspeed;
    PlanConfigValue maxaccel;
    PlanConfigValue maxdecel;
    PlanConfigValue maxturn;
    PlanConfigValue maxturnrate;
    PlanConfigValue maxturnangle;
} VehiclePlanConfig;

typedef union{
    ShipPlanConfig ship;
    PlanePlanConfig plane;
    VehiclePlanConfig vehicle;
} PlanConfigUnion;

/**
 * * ObjectPlan structure
 * * This structure holds the plan for an object, including its type and parameters.
 */

typedef float PlanValue;

typedef struct ShipPlanParams_s{
    PlanValue Len;
    PlanValue Width;
    PlanValue Height;
    PlanValue speed;
    PlanValue maxspeed;
    PlanValue maxaccel;
    PlanValue maxdecel;
    PlanValue maxturn;
    PlanValue maxturnrate;
    PlanValue maxturnangle;
    PlanValue maxpitch;
    PlanValue maxpitchrate;
} ShipPlanParams;

typedef struct PlanePlanParams_s{
    PlanValue WingAngle;
    PlanValue WingSpan;
} PlanePlanParams;

typedef struct VehiclePlanParams_s{
    PlanValue speed;
    PlanValue maxspeed;
    PlanValue maxaccel;
    PlanValue maxdecel;
    PlanValue maxturn;
    PlanValue maxturnrate;
    PlanValue maxturnangle;
} VehiclePlanParams;

typedef union{
    ShipPlanParams ship;
    PlanePlanParams plane;
    VehiclePlanParams vehicle;
} PlanUnion;

typedef struct ObjectPlan_s{
    ObjectTypeId type;
    PlanUnion plan;
    char name[128];
} ObjectPlan;

ObjectPlan* get_plan(int id);


float PlanValueRandomize(const PlanConfigValue *value);
void generate_plans(void);

#ifdef __cplusplus
}
#endif

#endif // PLAN_H