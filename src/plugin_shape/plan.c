#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plan.h"
#include "plan.h"
#include "shape.h"

ObjectPlan g_plans[MAX_OBJECT_PLANS];
const PlanConfigUnion g_planconfig[MAX_OBJECT_TYPES] = {
    [ShipFishing] = {
        .ship = {
            .Len = {2.0f, 0.1f},
            .Width = {0.4f, 0.1f},
            .Height = {0.4f, 0.1f},
            .speed = {20.0f, 5.0f},
            .maxspeed = {30.0f, 10.0f},
            .maxaccel = {5.0f, 2.0f},
            .maxdecel = {5.0f, 2.0f},
            .maxturn = {45.0f, 15.0f},
            .maxturnrate = {30.0f, 10.0f},
            .maxturnangle = {90.0f, 30.0f},
            .maxpitch = {15.0f, 5.0f},
            .maxpitchrate = {10.0f, 3.0f}
        }
    },
    [ShipCargo] = {
        .ship = {
            .Len = {20.0f, 10.0f},
            .Width = {5.0f, 2.0f},
            .Height = {3.0f, 1.5f},
            .speed = {15.0f, 5.0f},
            .maxspeed = {25.0f, 10.0f},
            .maxaccel = {4.0f, 1.5f},
            .maxdecel = {4.0f, 1.5f},
            .maxturn = {30.0f, 10.0f},
            .maxturnrate = {20.0f, 5.0f},
            .maxturnangle = {60.0f, 20.0f},
            .maxpitch = {10.0f, 3.0f},
            .maxpitchrate = {8.0f, 2.5f}
        }
    },
    [ShipPassenger] = {
        .ship = {
            .Len = {15.0f, 7.5f},
            .Width = {4.0f, 2.0f},
            .Height = {3.0f, 1.5f},
            .speed = {18.0f, 6.0f},
            .maxspeed = {28.0f, 8.0f},
            .maxaccel = {4.5f, 1.5f},
            .maxdecel = {4.5f, 1.5f},
            .maxturn = {35.0f, 10.0f},
            .maxturnrate = {25.0f, 7.5f},
            .maxturnangle = {70.0f, 20.0f},
            .maxpitch = {12.0f, 4.0f},
            .maxpitchrate = {9.0f, 3.0f}
        }
    },
    [ShipMilitary] = {
        .ship = {
            .Len = {25.0f, 12.5f},
            .Width = {6.0f, 3.0f},
            .Height = {4.0f, 2.0f},
            .speed = {22.0f, 7.5f},
            .maxspeed = {32.0f, 12.5f},
            .maxaccel = {6.0f, 2.5f},
            .maxdecel = {6.0f, 2.5f},
            .maxturn = {50.0f, 15.0f},
            .maxturnrate = {35.0f, 10.0f},
            .maxturnangle = {80.0f, 25.0f},
            .maxpitch = {18.0f, 6.0f},
            .maxpitchrate = {12.0f, 4.0f}
        }
    },
    [ShipAircraftCarrier] = {
        .ship = {
            .Len = {30.0f, 15.0f},
            .Width = {8.0f, 4.0f},
            .Height = {5.0f, 2.5f},
            .speed = {25.0f, 10.0f},
            .maxspeed = {35.0f, 15.0f},
            .maxaccel = {7.0f, 3.0f},
            .maxdecel = {7.0f, 3.0f},
            .maxturn = {60.0f, 20.0f},
            .maxturnrate = {40.0f, 15.0f},
            .maxturnangle = {90.0f, 30.0f},
            .maxpitch = {20.0f, 8.0f},
            .maxpitchrate = {15.0f, 5.0f}
        }
    },
    [PlanePassenger] = {
        .plane = {
            .WingAngle = {15.0f, 5.0f},
            .WingSpan = {30.0f, 10.0f}
        }
    },
    [PlaneMilitary] = {
        .plane = {
            .WingAngle = {20.0f, 7.5f},
            .WingSpan = {35.0f, 12.5f}
        }
    },
    [PlaneBalloon] = {
        .plane = {
            .WingAngle = {10.0f, 3.0f},
            .WingSpan = {25.0f, 8.0f}
        }
    },
};



float PlanValueRandomize(const PlanConfigValue *value) {
    float min = value->min;
    return min + ((float)rand() / RAND_MAX) * (value->delta);  // random value between min and max
}
void objectplan_randomize(ObjectPlan *plan, int plan_id) {
    // plan->type = rand() % (ShipAircraftCarrier+1); //MAX_OBJECT_TYPES;
    plan->type = ShipFishing; // for testing
    const PlanConfigUnion *config = &g_planconfig[plan->type];
    //plan->id = plan_id;
    (void)plan_id; // suppress unused variable warning
    switch(plan->type) {
        case ShipFishing:
        case ShipCargo:
        case ShipPassenger:
        case ShipMilitary:
        case ShipAircraftCarrier:
            plan->plan.ship.Len = PlanValueRandomize(&config->ship.Len);
            plan->plan.ship.Width = PlanValueRandomize(&config->ship.Width);
            plan->plan.ship.Height = PlanValueRandomize(&config->ship.Height);
            plan->plan.ship.speed = PlanValueRandomize(&config->ship.speed);
            plan->plan.ship.maxspeed = PlanValueRandomize(&config->ship.maxspeed);
            plan->plan.ship.maxaccel = PlanValueRandomize(&config->ship.maxaccel);
            plan->plan.ship.maxdecel = PlanValueRandomize(&config->ship.maxdecel);
            plan->plan.ship.maxturn = PlanValueRandomize(&config->ship.maxturn);
            plan->plan.ship.maxturnrate = PlanValueRandomize(&config->ship.maxturnrate);
            plan->plan.ship.maxturnangle = PlanValueRandomize(&config->ship.maxturnangle);
            plan->plan.ship.maxpitch = PlanValueRandomize(&config->ship.maxpitch);
            plan->plan.ship.maxpitchrate = PlanValueRandomize(&config->ship.maxpitchrate);
            break;
        case PlanePassenger:
        case PlaneMilitary:
        case PlaneBalloon: 
            plan->plan.plane.WingAngle = PlanValueRandomize(&config->plane.WingAngle);
            plan->plan.plane.WingSpan = PlanValueRandomize(&config->plane.WingSpan);
            break;
        case VehicleCar:
        case VehicleTruck:
        case VehicleBus:
        default:
            break;
    }

}
ObjectPlan* get_plan(int id){
    if (id < 0 || id >= MAX_OBJECT_PLANS) {
        return NULL;
    }
    return &g_plans[id];
}
void generate_plans(void) {
    for (int i = 0; i < MAX_OBJECT_PLANS; i++) {
        objectplan_randomize(&g_plans[i], i);
    }
}


void generate_shipplan(void) {
    // Generate ship plan here
}
