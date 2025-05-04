# Game plan
this document describes the game, which is a specific *example application* for the runtime environment, we have here. So other domcuemts may describes better the complate framework, like [Architect](architect.md), [Mapgen overview](overview.md). Lets figure out, what kind of game we could realize with ths system.

## Known limitations
Nowadays, the fast internet connection is a possibility, but mobiles and some landline regions are not the best on this question, therefore we still have some limitations in time domain. In case of a single player game, we can design the software to a usual gameer PC, and we will have a lot of memory, and GPU capacity there.
The good news is, even with a mobile device we can achive a high level of 3D computation fairly easy. But to provide the necessary visual information to the GPU supported display, we have to calculat and store datas on client side. This is a mandatory point to solve with the server/client architecture we would like to develop.
Other hand, there are huge differences between devices, which user's may use. So wide variety of the device capabilities, and user interfaes are there. To fulfill these goals, the client side may manage the bandwith by choose the texture resolution, model details, etc. Which means, the data can not be prepared as much as a single player / native game application can do. At server side, we try to limit the number of options for the resolution change or the options for bandwith setting, due to each different preparation may require more space on disk and RAM, and also cpu load need to be managed.

#### User interface challanges
The desktop app would be more simplier to implement, when developers could focus on a limited number of controllers, like mouse or joystick...
On Mobile devices, user click, tap, drag, multi-touch events could happen, also gyroscope can be usefull input. Even if the application is ...
... relies only on double-click, which is a common user input, it would be advisable to handle it in a user-dependent way on a touch device. The reason for this is that it is a quite sensitive input, which provides data of different quality on different devices, but users can also use it in quite extreme situations, so the touch event may be confused with a drag, or the double click with multi-touch on a mobile device's screen and the behavior of the browsers running on it can vary significantly.  
Overall, we must be prepared for a rather limited user experience on the web client, and design the human-machine interaction accordingly.  
The situation may differ slightly if we are talking about an installable application instead of a browser-based one, since (although differences still exist) the circumstances are somewhat easier to control.

#### Game context
A game designed for a personal computer can differ greatly from one designed for a mobile system—not only for the reasons discussed above but also due to user behavior. It is likely that players will want to engage with the game more frequently but for shorter sessions. Therefore, when they log in, they would prefer to be presented with a quick task. This differs from a PC game where the level of engagement and immersion tends to be deeper.

#### Interaction speed (lagging)
The game must remain enjoyable even when internet response times increase or the connection is lost entirely. Of course, the communication channel will be designed to handle these expected situations, but beyond that, the game itself must also support offline modes and provide autonomous gameplay possibilities—not just network-based ones.

## Goals
Motto: fast internet connection – that we intentionally do not fully utilize. The game flow should not rely on receiving very frequent updates from other players. If such a mode is needed, it should be highlighted as a separate environment, for example: a battle mode. But in general, we design the gameplay mostly around strategies, trade, exploration — meaning interactions that do not require intense real-time responsiveness. The multiplayer experience should come from the richness and variety of unique data: many diverse cities, many unique objects.  
These varied and diverse objects are partly delegated to a procedurally generated world, and partly influenced by the actions of the players.

## Open Questions

| Question | Why is it important? | Answer |
|----------|-----------------------|--------|
| Will there be a separate "light" mode for mobile? | For UI and network optimization | A bandwith management is planned. You're device may choose a low-res texture on-the-fly. |
| How modular will the game engine and logic be? | To support plugin-based development and feature extensibility | It is modular at server side. One of the goal of the plugin architecture is, to be replaceable during production live operation. (Some of the requirements are not handled, but long term goal.) |
| What is the expected number of concurrent players? | Affects server scaling, session and load management | The architecture is scalable.|
| Will there be any peer-to-peer connections, or will all communication go through the server? | Determines the network model and security/trust boundary | Nowadays, the direct p2p connection is limited (due to security reasons) therefore we do not plan to implement a p2p version. At least, it is not on the list now. |
| What part of the AI runs client-side vs server-side? | Influences offline capability and performance | User will focus on a limited region time-to-time, this part of the game engine will run on client side. Thease are typically animations of a pre-planned storyboard, in.example ship routes are on-going and repetitiv. In case of a new deal was made, booking is happen at client side. This will be one of the main complexity of the game... |
| How is game state saved? | Affects persistence and recovery during connection loss | We need a Failure Mode and Effect Analysis for this question. Yes obviously we will save everything on Server, when the player is online...|
| Will there be manual save/load or only autosave? | Impacts gameplay control and complexity | Ther will be an organised *leave* operation when user change focus or exit the game. But your interaction with the game engine is different than usual games. Something like a real life, when you plan the next days and weeks, and something similar happens... :) |
| Is cheating prevention needed, and where will validation happen? | Essential for integrity and security, especially in multiplayer | The serious answer is: we need an FMEA to completely cover all the possible problems. But there will be aways a rule based order, which all will be checked by the server. So, let assume you set how much you want buy/sell, the business will be active only if server was accepted, and even if you are offline, the server will ensure, your character can not spend more than the choosen amount... |


