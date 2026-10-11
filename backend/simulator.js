const { IoTDataPlaneClient, PublishCommand } = require("@aws-sdk/client-iot-data-plane");

const dev = process.argv[2] || "neernetra-01";
const iot = new IoTDataPlaneClient({ region: "ap-south-1" });
const profiles = {
  tap: [7.4, 220, 0.6],
  ro: [6.9, 40, 0.2],
  borewell: [7.8, 650, 1.5],
  dirty: [5.9, 900, 12],
};
const names = Object.keys(profiles);
const rnd = (a, b) => a + Math.random() * (b - a);

async function tick() {
  const name = names[Math.floor(Math.random() * names.length)];
  const [ph, tds, ntu] = profiles[name];
  const msg = {
    device_id: dev,
    ts: Math.floor(Date.now() / 1000),
    ph: ph + rnd(-0.2, 0.2),
    tds: tds + rnd(-20, 20),
    turbidity: Math.max(0, ntu + rnd(-0.3, 0.3)),
    temp: 26 + rnd(-1, 1),
    lat: 28.8955,
    lon: 76.6066,
  };
  await iot.send(new PublishCommand({
    topic: `neernetra/${dev}/data`,
    qos: 1,
    payload: Buffer.from(JSON.stringify(msg)),
  }));
  console.log(name, msg);
}

setInterval(tick, 5000);
tick();
