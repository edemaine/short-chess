size = 45
pad = 24
fontSize = 16
backgroundColor = '#fff'
labelColor = '#000'
files = 'abcdefgh'

export postprocess = (render) ->
  render.background backgroundColor
  {drawing} = render
  rows = drawing.keys.length
  cols = Math.max (row.length for row in drawing.keys)...
  width = cols * size
  height = rows * size

  labels = []
  for j in [0...cols]
    labels.push \
      <text x={(j + 0.5) * size} y={height + 18}
            text-anchor="middle">{files[j]}</text>
  for i in [0...rows]
    labels.push \
      <text x={-14} y={(i + 0.5) * size}
            text-anchor="middle" dominant-baseline="middle">{rows - i}</text>

  render.add \
    <g z-index="10" boundingBox="#{-pad} 0 #{width + pad} #{height + pad}"
       fill={labelColor} font-family="Arial, Helvetica, sans-serif"
       font-size={fontSize} font-weight="600">
      {labels}
    </g>
