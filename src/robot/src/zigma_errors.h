/* Verified app-server error table, plus English notification text.
 * See ERRORS.md. These codes are not voice IDs or navigation event IDs. */
#ifndef ZIGMA_ERRORS_H
#define ZIGMA_ERRORS_H
struct zigma_error { int code; const char *name,*description,*advice; };
static const struct zigma_error zigma_errors[]={
 {1,"LeftWheelSuspend","Left wheel is suspended","Place the robot on a level floor."},
 {2,"RollingBrushWinded","Main brush is tangled","Check the main brush for obstructions."},
 {3,"RightWheelSuspend","Right wheel is suspended","Place the robot on a level floor."},
 {4,"LeftWheelWinded","Left wheel is tangled","Check the left wheel for obstructions."},
 {5,"RightWheelWinded","Right wheel is tangled","Check the right wheel for obstructions."},
 {7,"SideBrushWinded","Side brush is tangled","Check the side brush for obstructions."},
 {9,"AbnormalCollisionSwitch","Collision sensor fault","Check that the bumper moves freely."},
 {10,"NoDustBox","Dustbin is missing","Check that the dustbin is installed correctly."},
 {11,"NoWaterBox","Water tank is missing","Check that the water tank is installed correctly."},
 {12,"AbnormalAntiFallingFunction","Cliff sensor fault","Clean the cliff sensors and place the robot on a level floor."},
 {15,"VacuumMotorTangled","Vacuum motor obstruction","Check the suction inlet and airflow path."},
 {16,"OperatingCurrentOverrun","Operating current is too high","Check for blocked wheels, brushes or airflow."},
 {17,"BottomNotOpenedWhenCharging","Bottom not opened while charging","The vendor description is ambiguous; inspect the robot and charging setup."},
 {19,"RobotStuck","Robot is stuck","Free the robot and clear nearby obstacles."},
 {20,"RobotBeTrapped","Robot is trapped","Move the robot to an accessible area."},
 {21,"LaserHeadCoverStuck","Laser cover is stuck","Check that the laser cover is unobstructed."},
 {22,"AbnormalLaserHead","Laser sensor fault","Check the laser sensor for obstructions."},
 {23,"WallSensorBlocked","Wall sensor is blocked","Clean the wall sensor."},
 {24,"VirtualWallForbiddenZoneSettingError","Virtual wall or forbidden zone configuration error","Review the map restrictions and the robot position."},
 {25,"ChargingError","Charging fault","Check the dock, charging contacts and power supply."},
 {26,"WheelSuspendInMidair","Robot lifted: wheels are off the floor","Place the robot on a level floor."},
};
static const struct zigma_error *zigma_error_lookup(int code,const char *name){
 for(size_t i=0;i<sizeof zigma_errors/sizeof *zigma_errors;i++)
  if(code>=0?zigma_errors[i].code==code:(name&&!strcmp(zigma_errors[i].name,name)))return &zigma_errors[i];
 return NULL;
}
static void zigma_error_text(char *out,size_t cap,int code,const char *name){
 const struct zigma_error *e=zigma_error_lookup(code,name);
 if(e)snprintf(out,cap,"%s (code %d)\n%s",e->description,e->code,e->advice);
 else if(code>0)snprintf(out,cap,"Unknown robot error (code %d)\nVendor status: %.64s",code,name?name:"unknown");
 else snprintf(out,cap,"Unmapped robot error: %.64s",name?name:"unknown");
}
#endif
