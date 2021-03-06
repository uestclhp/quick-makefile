/* Copyright (C) 2004-2011 IP Infusion, Inc. All Rights Reserved. */

#ifndef _HAL_AUTH_H_
#define _HAL_AUTH_H_

/* 802.1x port state. */
enum hal_auth_port_state
{
    HAL_AUTH_PORT_STATE_BLOCK_INOUT,
    HAL_AUTH_PORT_STATE_BLOCK_IN,
    HAL_AUTH_PORT_STATE_UNBLOCK,
    HAL_AUTH_PORT_STATE_UNCONTROLLED
};

/*MAC-based authentication Enhancement*/
enum hal_auth_mac_port_state
{
    HAL_MACAUTH_PORT_STATE_ENABLED,
    HAL_MACAUTH_PORT_STATE_DISABLED,
};

/* 
   Name: hal_auth_init

   Description:
   This API initializes the hardware layer for 802.1x port authentication.

   Parameters:
   None

   Returns:
   HAL_ERR_AUTH_INIT
   HAL_SUCCESS
*/
int hal_auth_init (void);

/* 
   Name: hal_auth_deinit

   Description:
   This API deinitializes the hardware layer for 802.1x port authentication.

   Parameters:
   None

   Returns:
   HAL_ERR_AUTH_INIT
   HAL_SUCCESS
*/
int hal_auth_deinit (void);

/* 
   Name: hal_auth_set_port_state

   Description:
   This API sets the authorization state of a port 

   Parameters:
   IN -> index - port index
   IN -> state - port state
   
   Returns:
   < 0 on error
   HAL_SUCCESS
*/
int
hal_auth_set_port_state (unsigned int index, enum hal_auth_port_state state);

int
hal_auth_mac_set_port_state (unsigned int index, int mode,
                             enum hal_auth_mac_port_state state);

int hal_auth_mac_unset_irq (unsigned int index);

struct hal_auth_ops
{
  int (*init)(void);
  int (*deinit)(void);
  int (*set_port_state)(unsigned int index, enum hal_auth_port_state state);
  int (*mac_set_port_state)(unsigned int index, int mode,
                             enum hal_auth_mac_port_state state);
  int (*mac_unset_irq)(unsigned int index);

};
#endif /* _HAL_AUTH_H_ */
