const { DynamoDBClient } = require("@aws-sdk/client-dynamodb");
const { DynamoDBDocumentClient, PutCommand } = require("@aws-sdk/lib-dynamodb");
const { SNSClient, PublishCommand } = require("@aws-sdk/client-sns");

const ddb = DynamoDBDocumentClient.from(new DynamoDBClient({}));
const sns = new SNSClient({});
const TABLE = process.env.TABLE;
const TOPIC = process.env.ALERT_TOPIC || "";

// Indicative rules based on BIS IS 10500 (verify limits from official source).
function classify({ ph, tds, turbidity }) {
  const reasons = [];
  let level = 0; // 0 safe, 1 caution, 2 unsafe
  if (ph < 6.5 || ph > 8.5) { level = 2; reasons.push(`pH ${ph.toFixed(1)} outside 6.5-8.5`); }
  if (tds > 2000) { level = 2; reasons.push(`TDS ${tds.toFixed(0)} very high`); }
  else if (tds > 500) { level = Math.max(level, 1); reasons.push(`TDS ${tds.toFixed(0)} above 500 mg/L`); }
  if (turbidity > 5) { level = 2; reasons.push(`Turbidity ${turbidity.toFixed(1)} NTU above 5`); }
  else if (turbidity > 1) { level = Math.max(level, 1); reasons.push(`Turbidity ${turbidity.toFixed(1)} NTU above 1`); }

  const status = ["SAFE", "CAUTION", "UNSAFE"][level];
  const advice = {
    SAFE: "Paani theek lag raha hai. Peene se pehle lab test zaroor karwayein.",
    CAUTION: "Paani ko filter ya RO se guzaren aur ubaal kar piyen.",
    UNSAFE: "Is paani ko na piyen. Doosra source use karein ya treat karwayein.",
  }[status];
  return { status, reasons, advice };
}

exports.handler = async (event) => {
  const { status, reasons, advice } = classify(event);
  await ddb.send(new PutCommand({ TableName: TABLE, Item: { ...event, status, reasons, advice } }));

  if (status === "UNSAFE" && TOPIC) {
    await sns.send(new PublishCommand({
      TopicArn: TOPIC,
      Subject: "NeerNetra: UNSAFE water",
      Message: `Device ${event.device_id}: ${reasons.join("; ")}\n${advice}`,
    }));
  }
  return { status };
};
