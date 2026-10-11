const { DynamoDBClient } = require("@aws-sdk/client-dynamodb");
const { DynamoDBDocumentClient, QueryCommand } = require("@aws-sdk/lib-dynamodb");

const ddb = DynamoDBDocumentClient.from(new DynamoDBClient({}));
const TABLE = process.env.TABLE;

exports.handler = async (event) => {
  const q = event.queryStringParameters || {};
  const device = q.device || "neernetra-01";
  const limit = Math.min(parseInt(q.limit || "50", 10), 200);

  const res = await ddb.send(new QueryCommand({
    TableName: TABLE,
    KeyConditionExpression: "device_id = :d",
    ExpressionAttributeValues: { ":d": device },
    ScanIndexForward: false,
    Limit: limit,
  }));

  return {
    statusCode: 200,
    headers: { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" },
    body: JSON.stringify(res.Items || []),
  };
};
