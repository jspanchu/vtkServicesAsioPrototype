# Description:

Investigate communication b/w threads and processes asynchronously with asio and rxcpp.

# Compile:

```bash
 $ export RXCPP_DIR=/path/to/rxcpp/source
 $ export VTK_DIR=/path/to/vtk/build
 $ mkdir-p build && cd build && cmake -GNinja
 $ ninja
```

# Run:

Launch a server on port 1234

`$ ./main -s 1234 &`

Connect to a server and execute 100 remote commands.

`$ ./main -c localhost 1234 -n100`

# Sample output:

Here is a sample output when run as a server. Before a client connected, some command line interaction shows how the
server main thread is free.

```bash
$ ./main -s 1234
Now simulating repl-like interaction with server.
Please prefix a valid service name.
Example: services.data:update
> services.data:update-pipeline
> 2023-01-09 23:30:13.878 (  10.778s) [services.data   ]               main.cxx:129   INFO| update-pipeline

> services.render:render
> 2023-01-09 23:30:23.090 (  19.990s) [services.render ]               main.cxx:129   INFO| render

> services.io:read
> 2023-01-09 23:30:27.365 (  24.265s) [services.io     ]               main.cxx:129   INFO| read

> 2023-01-09 23:30:29.357 (  26.257s) [        A3599700]               main.cxx:484   INFO| Accepted connection!
2023-01-09 23:30:29.367 (  26.267s) [services.render ]               main.cxx:129   INFO| render-4
2023-01-09 23:30:29.367 (  26.267s) [services.io     ]               main.cxx:129   INFO| write-2
2023-01-09 23:30:29.367 (  26.267s) [services.render ]               main.cxx:129   INFO| render-3
2023-01-09 23:30:29.367 (  26.267s) [services.io     ]               main.cxx:129   INFO| write
2023-01-09 23:30:29.367 (  26.267s) [services.io     ]               main.cxx:129   INFO| read-3
2023-01-09 23:30:29.367 (  26.267s) [services.data   ]               main.cxx:129   INFO| update-state
2023-01-09 23:30:29.367 (  26.267s) [services.data   ]               main.cxx:129   INFO| update-state
2023-01-09 23:30:29.367 (  26.267s) [services.data   ]               main.cxx:129   INFO| object-delete
2023-01-09 23:30:29.367 (  26.267s) [services.data   ]               main.cxx:129   INFO| update-state
2023-01-09 23:30:29.367 (  26.267s) [services.data   ]               main.cxx:129   INFO| update-pipeline
2023-01-09 23:30:36.900 (  33.800s) [services.data   ]               main.cxx:129   INFO| update-state
2023-01-09 23:30:40.957 (  37.857s) [services.io     ]               main.cxx:129   INFO| read
2023-01-09 23:30:46.034 (  42.934s) [services.render ]               main.cxx:129   INFO| render
2023-01-09 23:30:47.604 (  44.504s) [        A3599700]               main.cxx:202    ERR| Failed to read header. Closing socket.
2023-01-09 23:30:47.604 (  44.504s) [        A3599700]               main.cxx:473    ERR| Error code 2
^C

```

Here is the sample output from a client. After simulating automated communications,
the client waits for user input to simulate interactive communication, typical in a GUI/CLI application.

```bash
$ ./main -c localhost 1234 -n10
2023-01-09 23:30:29.357 (   0.000s) [        2D2EB700]               main.cxx:355   INFO| Connected!
2023-01-09 23:30:29.357 (   0.000s) [        2D2EB700]               main.cxx:66    INFO| => services.render:15335038379579459937
2023-01-09 23:30:29.357 (   0.000s) [        2D2EB700]               main.cxx:66    INFO| => services.data:14752253762927134915
2023-01-09 23:30:29.357 (   0.000s) [        2D2EB700]               main.cxx:66    INFO| => services.io:15497323717503977043
2023-01-09 23:30:29.367 (   0.011s) [response        ]               main.cxx:652   INFO| reply: response-render-4
2023-01-09 23:30:29.367 (   0.011s) [response        ]               main.cxx:652   INFO| reply: response-write-2
2023-01-09 23:30:29.367 (   0.011s) [response        ]               main.cxx:652   INFO| reply: response-render-3
2023-01-09 23:30:29.367 (   0.011s) [response        ]               main.cxx:652   INFO| reply: response-write
2023-01-09 23:30:29.367 (   0.011s) [response        ]               main.cxx:652   INFO| reply: response-read-3
2023-01-09 23:30:29.367 (   0.011s) [response        ]               main.cxx:652   INFO| reply: response-update-state
2023-01-09 23:30:29.367 (   0.011s) [response        ]               main.cxx:652   INFO| reply: response-update-state
2023-01-09 23:30:29.367 (   0.011s) [response        ]               main.cxx:652   INFO| reply: response-object-delete
2023-01-09 23:30:29.367 (   0.011s) [response        ]               main.cxx:652   INFO| reply: response-update-state
2023-01-09 23:30:29.367 (   0.011s) [response        ]               main.cxx:652   INFO| reply: response-update-pipeline
Sent(10). Received(10)
Now simulating interactive communication.
Please prefix a valid service name.
Example: services.data:update
> services.data:update-state
> 2023-01-09 23:30:36.900 (   7.544s) [response        ]               main.cxx:652   INFO| reply: response-update-state

> services.io:read
> 2023-01-09 23:30:40.957 (  11.601s) [response        ]               main.cxx:652   INFO| reply: response-read

> services.render:render
> 2023-01-09 23:30:46.034 (  16.678s) [response        ]               main.cxx:652   INFO| reply: response-render

> ^C
```
