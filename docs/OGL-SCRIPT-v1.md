# OGL Script Language v1

OGL is the native OpenGenesisLINK scripting language introduced in 9.0.0-dev.

Its design goal is a small, explicit server language that maps directly to OpenGenesisLINK services instead of reproducing historical viewer/server quirks.

## Basic form

```text
@ogl 1

state default
on touch
let count = 1
inc count by 2
world.say Hello
world.move 128 128 25
goto active
end

state active
on timer
world.text Active
end
```

Semicolons are optional. `//` and `#` comments are accepted where the frontend permits them.

## State and event declarations

- `state <name>`
- `on <event>`
- `end`
- `goto <state>`

## Variables

- `let <name> = <value>`
- `inc <name> by <integer>`

Values beginning with `$` in the shared IR resolve persistent variables. OGL v1 deliberately keeps its public variable syntax minimal.

## Runtime and host commands

- `emit <value>`
- `timer.every <milliseconds>`
- `chat.listen <channel>`
- `owner.notify <text>`
- `user.message <user-id> <text>`
- `stop`

## World mutations

- `world.move <x> <y> <z>`
- `world.rotate <x> <y> <z>`
- `world.scale <x> <y> <z>`
- `world.velocity <x> <y> <z>`
- `world.angular_velocity <x> <y> <z>`
- `world.physics <0|1>`
- `world.text <text>`
- `world.say <text>`
- `world.whisper <text>`
- `world.shout <text>`

All World mutations travel through the durable Script World queue and are revalidated by the owning World Node.

## Queries

- `world.object <prefix>`
- `world.region <prefix>`
- `world.terrain <prefix>`
- `world.water <prefix>`
- `world.time <prefix>`
- `world.nearby <prefix> <radius>`

Results are asynchronous and written into prefixed persistent VM variables.

## OGL v1 boundaries

The current public catalog contains 31 language/features. 27 are implemented in 9.0.

Not yet implemented as native OGL syntax:

- structured `if/else`
- `while/for`
- user-defined functions
- a typed value system

These are explicit roadmap items rather than hidden parser behavior.
