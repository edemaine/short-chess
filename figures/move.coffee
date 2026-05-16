size = 45
moveColor = 'hsl(286,65%,50%,70%)'

center = ([x, y]) ->
  [x * size + size / 2, y * size + size / 2]

export preprocess = (render) ->
  render.lastMoves = []
  for row, y in render.drawing.keys
    for key, x in row
      continue unless typeof key == 'string'
      from = key.includes '<'
      to = key.includes '>'
      clean = key.replace /[<>]/g, ''
      clean = '.' if clean == ''
      render.drawing.set x, y, clean
      render.lastMoves.push {from, to, x, y} if from or to

export postprocess = (render) ->
  from = render.lastMoves.find (move) -> move.from
  to = render.lastMoves.find (move) -> move.to
  return unless from? and to?

  [x1, y1] = center [from.x, from.y]
  [x2, y2] = center [to.x, to.y]
  render.add \
    <line x1={x1} y1={y1} x2={x2} y2={y2}
          stroke={moveColor} stroke-width="10" stroke-linecap="round"
          z-index="-0.5"/>
