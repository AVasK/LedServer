# LedServer
A simple asynchronous server &amp; synchronous client pair

> Uses boost::asio for networking.

Server displays the current LED state.


Client has several commands:
- on     | turns the LED on
- off    | turns the LED off
- state? | current LED state
- red    | led.color <- red
- green  | led.color <- green
- blue   | led.color <- blue
- color? | current LED color
- rate 1..5 | sets LED rate in 0..5 interval
- rate?  | returns the current LED rate
